#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record a performance on the FM-1, then film it: the screen exactly as the FM-1 drew it, with its sound.

    tools/capture.py record [NAME]      play on the FM-1 between two presses of Enter -> takes/NAME/ and NAME.mp4
    tools/capture.py render takes/NAME  film a take again (e.g. with other options)

Options: --fps 30 (or 60), --size 720 (the video's side, pixels), --tail 3 (seconds after the last event: the
release and the reverb), --splash (start the film with the power-on animation).

While recording, the FM-1 (firmware with capture.c) sends what it starts from (the patches, the sound, the
settings, the page) and then every key, button, knob turn and MIDI message it receives, timed. The take is then
played again through the firmware's own code on this computer (tests/host.c: the same engine and UI), a frame
each 1/fps second, and ffmpeg makes the MP4. Needs mido + python-rtmidi, a C compiler and ffmpeg (or
pip's imageio-ffmpeg).
Not captured: the MASTER knob (the film is at full level) and the MIDI clock (a take on CLOCK = MIDI plays on the
internal tempo)."""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAKES = ROOT / "takes"
LEAD_MS = 100                                   # the film starts this long before the first event
SPLASH_MS = 2700                                # (--splash: the animation, then the take)


def find_port(names):
    for n in names:
        if "felucca" in n.lower() or "fm-1" in n.lower() or "prysm" in n.lower():
            return n
    sys.exit("no FM-1 among the MIDI ports: " + ", ".join(names))


def record(name):
    import mido
    inp_name, out_name = find_port(mido.get_input_names()), find_port(mido.get_output_names())
    got, lock = [], threading.Lock()

    def on_msg(m):
        if m.type == "sysex" and len(m.data) >= 3 and m.data[0] == 0x7D and m.data[1] == 0x50:
            with lock:
                got.append(bytes(m.data[2:]))

    with mido.open_input(inp_name, callback=on_msg) as _, mido.open_output(out_name) as out:
        input(f"FM-1 on {inp_name}. Set it up (no keys held), then press Enter to start recording... ")
        out.send(mido.Message("control_change", channel=15, control=119, value=127))
        t0 = time.time()
        input("Recording: play now. Press Enter to stop... ")
        out.send(mido.Message("control_change", channel=15, control=119, value=0))
        time.sleep(0.5)
    with lock:
        msgs = list(got)
    state, n_state, events = bytearray(), None, []
    for m in msgs:
        kind, d = chr(m[0]), m[1:]
        if kind == "S":
            i = (d[0] << 7 | d[1]) * 64
            chunk = bytes(d[2 + 2 * j] << 4 | d[3 + 2 * j] for j in range(64))
            if len(state) < i + 64:
                state.extend(bytes(i + 64 - len(state)))
            state[i:i + 64] = chunk
        elif kind == "T":
            n_state = d[0] << 14 | d[1] << 7 | d[2]
        elif kind in "KBNMX":
            events.append((d[0] << 14 | d[1] << 7 | d[2], kind, d[3:]))
    if n_state is None:
        sys.exit("the FM-1 sent no take: is its firmware one with capture (0.8 or later)?")
    d = TAKES / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "state.bin").write_bytes(bytes(state[:n_state]))
    lines = [f"# take {name}: recorded {time.strftime('%Y-%m-%d %H:%M')}, {time.time() - t0:.1f} s"]
    last = 0
    for t, kind, a in events:
        last = max(last, t)
        if kind == "K":
            lines.append(f"{t} keys 0x{a[0] << 21 | a[1] << 14 | a[2] << 7 | a[3]:x}")
        elif kind == "B":
            mask = a[0] << 7 | a[1]
            lines += [f"{t} btn {b}" for b in range(14) if mask >> b & 1]
        elif kind == "N":
            lines.append(f"{t} enc {a[0]} {a[1] - 64}")
        elif kind == "M":
            lines.append(f"{t} midi 0x{a[0] << 7 | a[1]:02x} {a[2]} {a[3]}")
    lines.append(f"{last} last")
    (d / "events.txt").write_text("\n".join(lines) + "\n")
    print(f"{d}: {len(events)} events, {last / 1000:.1f} s")
    return d


def render(d, fps, size, tail, splash):
    d = Path(d)
    cc = shutil.which("cc") or sys.exit("no C compiler (cc)")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:                              # (or the one pip's imageio-ffmpeg brings)
        try:
            import imageio_ffmpeg
            ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        except ImportError:
            sys.exit("no ffmpeg: brew install ffmpeg, or pip install imageio-ffmpeg")
    work = d / "frames"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    exe = work / "prysm"
    ver = subprocess.run(["git", "describe", "--tags", "--always"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    flags = ["-O2", "-w", "-DDINGHY_HOST", f'-DAPP_VER="{ver or "dev"}"', f"-I{ROOT / 'firmware/src'}",
             f"-I{ROOT / 'firmware/gen'}"] + (["-DPRYSM_SPLASH"] if splash else [])
    subprocess.run([cc, *flags, "-o", str(exe), str(ROOT / "firmware/src/prysm.c")], check=True)
    off = LEAD_MS + (SPLASH_MS if splash else 0)
    script, last = [], 0
    for line in (d / "events.txt").read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        t, rest = line.split(" ", 1)
        if rest == "last":
            last = int(t)
            continue
        script.append(f"{int(t) + off} {rest}")
    script.append(f"{last + off + int(tail * 1000)} end")
    (work / "script.txt").write_text("\n".join(script) + "\n")
    env = dict(os.environ)
    if (d / "state.bin").exists():              # (none: a scripted take, from the default sound)
        env["PRYSM_STATE"] = str(d / "state.bin")
    print(f"playing the take again ({(last + off) / 1000 + tail:.1f} s at {fps} fps)...")
    subprocess.run([str(exe), str(work / "f"), str(work / "script.txt"), str(fps)], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    mp4 = d.parent / f"{d.name}.mp4"
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", str(work / "f-%05d.ppm"),
                    "-i", str(work / "f.wav"), "-vf", f"scale={size}:{size}:flags=neighbor", "-c:v", "libx264",
                    "-pix_fmt", "yuv420p", "-crf", "14", "-c:a", "aac", "-b:a", "256k", "-shortest", str(mp4)],
                   check=True)
    shutil.copy(work / "f.wav", d / "audio.wav")
    shutil.rmtree(work)
    print(f"-> {mp4} (and {d / 'audio.wav'}: the sound alone)")
    return mp4


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["record", "render"])
    ap.add_argument("name", nargs="?", help="record: the take's name; render: its folder")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--size", type=int, default=720)
    ap.add_argument("--tail", type=float, default=3.0)
    ap.add_argument("--splash", action="store_true")
    a = ap.parse_args()
    if a.what == "record":
        d = record(a.name or time.strftime("take-%Y%m%d-%H%M%S"))
    else:
        if not a.name:
            ap.error("render: which take (its folder)?")
        d = a.name
    render(d, a.fps, a.size, a.tail, a.splash)


if __name__ == "__main__":
    main()
