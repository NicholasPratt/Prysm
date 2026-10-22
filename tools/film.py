#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Films made of scripted scenes, played through the firmware's own code (tools/capture.py render): the video manual
(tools/films/basics.py) and others like it.

A Scene is written as a player plays it (page buttons, SELECT, knob turns, keys); the scenes of a film run on from one
another, the sound as the last one left it: this module plays every turn on the patch as the firmware does (the knob
tables come from the firmware itself: emu/params.c), so each scene starts exactly where the one before ended, from the
default sound a viewer can load too. Each caption says what is going on; under it, "On the FM-1" lists the controls
the scene used meanwhile, by their names on the FM-1, so a viewer can follow along. A caption stays up at least as
long as it takes to read.

    film(scenes, name, title)  -> takes/NAME/NAME.mp4 (1920 x 1080), .srt (the captions), chapters.txt (YouTube's)"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

# ---- the firmware's knobs and default sound

PAGES = {"OSC1": "OSC 1", "OSC2": "OSC 2", "FILTER": "FILTER", "AMP": "AMP", "FENV": "F.ENV", "MENV": "M.ENV 1",
         "MENV2": "M.ENV 2", "LFO": "LFO", "LFO2": "LFO 2",
         "MIX": "MIX", "ARP": "ARP", "SEQ": "SEQ", "CHO": "CHORUS", "DLY": "DELAY", "REV": "REVERB",
         "SETUP": "SETUP", "PATCH": "PATCH", "SCOPE": "SCOPE"}   # (ui.c's order: PG_OSC1 .. PG_SETUP, PG_PATCH, PG_SCOPE)
PG = {p: i for i, p in enumerate(PAGES)}
ROWS = {"OSC1": ["WAVE", "WAVE2", "RATIO", "MODE", "ENV", "MIX"], "OSC2": ["WAVE", "WAVE2", "RATIO", "MODE", "ENV", "MIX"],
        "FILTER": ["FILTER", "TYPE", "COMB"], "AMP": ["ENV", "VEL"], "FENV": ["ENV"], "MENV": ["ENV", "DEST"],
        "MENV2": ["ENV", "DEST"], "LFO": ["LFO", "DEST"],
        "LFO2": ["LFO", "DEST"], "MIX": ["OSC", "GLIDE"], "ARP": ["ARP", "TIME"], "SEQ": ["STEP", "CHORD", "PATTERN", "EDIT", "CLICK", "SONG", "ENTRY"],
        "CHO": ["CHORUS"], "DLY": ["DELAY", "D.MOD"], "REV": ["REVERB", "R.MOD"], "SETUP": ["MIDI"],
        "PATCH": ["SAVE", "LOAD", "INIT"], "SCOPE": []}
BTN = {"FX": 2, "SELECT": 3, "ENV": 4, "LFO": 5, "EDIT": 6, "GLO": 7, "HOME": 8, "SAVE": 9, "ARP": 10, "SEQ": 11,
       "PLAY": 12, "REC": 13}
ENC_SELECT, ENC_KNOB1, ENC_PRESETS = 0, 2, 6
TURN_MS = 70                                    # between knob events: slower than the UI's fast turn (60 ms: x 3)
STEPPED = {7, 9, 11, 16, 22, 25, 28, 29}        # ui.c's kinds that move a step per two detents (modes, waves, on / off)
SKIP_DEST = {13, 14}                            # (LFORATE, LFOAMT: the mod envelope's own; the LFOs' knobs skip them)

_info = None


def cc():
    return shutil.which("cc") or sys.exit("no C compiler (cc)")


def info():
    """The firmware's default sound (bytes), its knobs {(page, row, knob): (offset, max, min, kind, label)} and the
    offsets of a few fields: compiled from the firmware itself."""
    global _info
    if _info:
        return _info
    src = ('#define main prysm_host_main\n#include "prysm.c"\n#undef main\n#include "../../emu/params.c"\n'
           'int main(void)\n{\n    uint32_t i;\n    edit = PATCH_INIT;\n'
           '    for (i = 0; i < sizeof edit; i++) printf("%u ", ((uint8_t *)&edit)[i]);\n    printf("\\n");\n'
           '    for (i = 0; i < prm_n(); i++) printf("%s\\n", prm_info(i));\n'
           '    printf("seq %u\\n", (unsigned)__builtin_offsetof(patch_t, arp_on));\n'
           '    return 0;\n}\n')
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "info.c").write_text(src)
        subprocess.run([cc(), "-O0", "-w", "-DDINGHY_HOST", f"-I{ROOT / 'firmware/src'}", f"-I{ROOT / 'firmware/gen'}",
                        "-o", str(t / "info"), str(t / "info.c")], check=True)
        out = subprocess.run([str(t / "info")], capture_output=True, text=True, check=True).stdout.splitlines()
    init = bytes(int(x) for x in out[0].split())
    knobs = {}
    names = {v: k for k, v in PAGES.items()}
    for line in out[1:-1]:
        page, row, label, knob, off, mx, mn, kind = line.split("|")
        knobs.setdefault((names[page], row, int(knob)), (int(off), int(mx), int(mn), int(kind), label))   # (not PIT1, PIT2)
    s = [int(x) for x in out[-1].split()[1:]]
    _info = {"init": init, "knobs": knobs, "arp_on": s[0]}
    return _info


def gen_states(states):
    """Each (path, patch bytes, page, rows {page: row}, seq cursor, PATCH screen row): a take's start state (capture.c's cap_state),
    the sound as given, the default as the last saved (so a changed sound shows its *), no slot."""
    body = []
    for i, (path, patch, pg, rows, cursor, pa_row) in enumerate(states):
        body.append(f"    {{ static const uint8_t b[] = {{{','.join(str(x) for x in patch)}}};\n"
                    f"      memcpy(&edit, b, sizeof edit); }}\n"
                    f"    clean = PATCH_INIT;\n    cur_slot = -1;\n    gset.slot = -1;\n    page = {pg};\n"
                    f"    memset(row, 0, sizeof row);\n    pa_slot = 0;\n    pa_row = {pa_row};\n    seq_cursor = {cursor};")
        body += [f"    row[{p}] = {n};" for p, n in rows.items()]
        body.append(f"    n = cap_state(cap_buf, 0);\n    g = fopen(\"{path}\", \"wb\");\n"
                    f"    fwrite(cap_buf, 1, n, g);\n    fclose(g);")
    src = ('#define main prysm_host_main\n#include "prysm.c"\n#undef main\nint main(void)\n{\n    FILE *g;\n'
           '    uint32_t n;\n    bank.version = PATCH_VERSION;\n    seqs_init();\n' + "\n".join(body) + "\n    return 0;\n}\n")
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "states.c").write_text(src)
        subprocess.run([cc(), "-O0", "-w", "-DDINGHY_HOST", f"-I{ROOT / 'firmware/src'}", f"-I{ROOT / 'firmware/gen'}",
                        "-o", str(t / "states"), str(t / "states.c")], check=True)
        subprocess.run([str(t / "states")], check=True)


# ---- a scene

def reading_ms(*texts):                         # how long a caption takes to read: 2.7 words a second, and a breath
    words = sum(len(t.split()) for t in texts)
    return int(1500 + words / 2.7 * 1000)


class Scene:
    """A take, written as a player plays it; times in ms from its start (t: where the next action goes). after:
    the scene it carries on from (its sound, page and rows), or None for the default sound on OSC 1."""

    def __init__(self, name, chapter, after=None, splash=False, tail=3.0):
        I = info()
        self.name, self.chapter, self.splash, self.tail = name, chapter, splash, tail
        if after:
            self.patch, self.page, self.rows = bytearray(after.patch), after.page, dict(after.rows)
            self.back, self.rec, self.cursor = after.back, after.rec, after.cursor
        else:
            self.patch, self.page, self.rows = bytearray(I["init"]), "OSC1", {p: 0 for p in PAGES}
            self.back, self.rec, self.cursor = "OSC1", False, 0
        self.pa_slot = 0
        self.start = (bytes(self.patch), PG[self.page], {PG[p]: n for p, n in self.rows.items() if n and PG[p] < 13},
                      self.cursor, self.rows["PATCH"])
        self.t, self.ev, self.caps, self.how = 0, [], [], []

    # time and captions
    def wait(self, ms):
        self.t += ms
        return self

    def caption(self, title, body=""):         # from now until the next one (after the last one has been read)
        if self.caps:
            t0, a, b, _ = self.caps[-1]
            self.t = max(self.t, t0 + reading_ms(a, b))
            self.caps[-1][3] = self.how
        self.caps.append([self.t, title, body, None])
        self.how = []
        return self

    def done(self):                             # the end: the last caption read, and its controls noted
        if self.caps:
            t0, a, b, _ = self.caps[-1]
            self.t = max(self.t, t0 + reading_ms(a, b))
            self.caps[-1][3] = self.how
        return self

    def _how(self, s):
        if not self.how or self.how[-1] != s:
            self.how.append(s)

    # the controls
    def press(self, b, gap=500):
        self.ev.append((self.t, f"btn {BTN[b]}"))
        p, I = self.page, info()
        if b == "HOME":
            self.page = self.back if p == "SCOPE" else p if p == "PATCH" else "SCOPE"
            if p not in ("SCOPE", "PATCH"): self.back = p
        elif b == "SAVE":
            if p == "PATCH":                    # (do it: SAVE, LOAD or INIT; the sound unchanged by a save)
                if self.rows["PATCH"] == 2: self.patch = bytearray(I["init"])
            else:
                if p != "SCOPE": self.back = p
                self.page = "PATCH"
        elif b == "EDIT": self.page = "OSC2" if p == "OSC1" else "OSC1"
        elif b == "FX": self.page = {"CHO": "DLY", "DLY": "REV"}.get(p, "CHO")
        elif b == "ENV": self.page = {"AMP": "FENV", "FENV": "MENV", "MENV": "MENV2"}.get(p, "AMP")
        elif b == "SELECT": self.page = "FILTER"
        elif b == "LFO": self.page = "LFO2" if p == "LFO" else "LFO"
        elif b == "GLO": self.page = "SETUP" if p == "MIX" else "MIX"
        elif b == "SEQ": self.page = "SEQ"
        elif b == "REC": self.page, self.rec = "SEQ", not self.rec
        elif b == "ARP":
            if p == "ARP": self.patch[I["arp_on"]] ^= 1
            else: self.page = "ARP"
        self._how(f"press {b}" if b != "SELECT" else "press SELECT (the button)")
        return self.wait(gap)

    def go(self, page):                         # to a page by its button, as a player would
        way = {"OSC1": "EDIT", "OSC2": "EDIT", "FILTER": "SELECT", "AMP": "ENV", "FENV": "ENV", "MENV": "ENV",
               "MENV2": "ENV", "LFO": "LFO", "LFO2": "LFO",
               "MIX": "GLO", "SETUP": "GLO", "ARP": "ARP", "SEQ": "SEQ", "CHO": "FX", "DLY": "FX", "REV": "FX",
               "SCOPE": "HOME", "PATCH": "SAVE"}[page]
        for _ in range(5):
            if self.page == page: return self
            self.press(way)
        sys.exit(f"{self.name}: no way to {page}")

    def row(self, name):                        # SELECT (the knob): a row of the page
        d = ROWS[self.page].index(name) - self.rows[self.page]
        if d:
            self.ev.append((self.t, f"enc {ENC_SELECT} {d}"))
            self.rows[self.page] += d
            self._how(f"turn SELECT to {name}")
            self.wait(600)
        return self

    def _knob(self, knob):
        if knob == "cut":
            return info()["knobs"][("FILTER", "FILTER", 0)][:3] + (1, "CUTOFF")
        return info()["knobs"].get((self.page, ROWS[self.page][self.rows[self.page]], knob - 1))

    def turn(self, knob, to, ms=1500, block=True):   # knob 1..4 or "cut" (PRESETS) to a value, over ms
        k = self._knob(knob)
        if not k: sys.exit(f"{self.name}: no knob {knob} on {self.page} {ROWS[self.page][self.rows[self.page]]}")
        off, mx, mn, kind, label = k
        cur, to = self.patch[off], max(mn, min(mx, to))
        steps = to - cur
        if kind in (33, 34):                    # (the LFOs' destinations skip the mod envelope's two)
            steps -= sum(1 for v in SKIP_DEST if min(cur, to) < v < max(cur, to))
        detents = steps * (2 if kind in STEPPED else 1)
        enc = ENC_PRESETS if knob == "cut" else ENC_KNOB1 + knob - 1
        n = max(1, min(abs(detents), ms // TURN_MS)) if detents else 0
        done, t = 0, self.t
        for i in range(n):
            d = round(detents * (i + 1) / n) - done
            if d: self.ev.append((t, f"enc {enc} {d}"))
            done += d
            t += TURN_MS
        self.patch[off] = to
        self._how(f"turn PRESETS ({label})" if knob == "cut" else f"turn KNOB {knob} ({label})")
        if block: self.t = t + 250
        return self

    def patch_row(self, name):                  # on the PATCH screen: SAVE, LOAD or INIT
        return self.row(name)

    def slot(self, n):                          # on the PATCH screen: KNOB 1 to slot n (1 .. 8)
        d = (n - 1) - self.pa_slot
        if d:
            self.ev.append((self.t, f"enc {ENC_KNOB1} {d}"))
            self.pa_slot = n - 1
            self._how(f"turn KNOB 1 to slot {n}")
            self.wait(600)
        return self

    def on(self, *notes, vel=127):
        I = info()
        for n in notes:                         # (with REC, the firmware enters them in the pattern: not the sound)
            self.ev.append((self.t, f"midi 0x90 {n} {vel}"))
            if self.rec and self.page == "SEQ":
                self.cursor = (self.cursor + 1) % 16
        self._how("play keys")
        return self

    def off(self, *notes):
        self.ev += [(self.t, f"midi 0x80 {n} 0") for n in notes]
        return self

    def play(self, notes, dur=250, gap=300, at=None):   # one after another (a chord: a tuple); at: without moving t
        t, keep = (self.t if at is None else at), self.t
        for n in notes:
            ns = n if isinstance(n, tuple) else (n,)
            self.t = t
            self.on(*ns)
            self.t = t + dur
            self.off(*ns)
            t += gap
        self.t = t if at is None else keep
        return self


# ---- the picture: the screen at 4x on the left, the captions on the right

FONT = "/System/Library/Fonts/HelveticaNeue.ttc"
W, H, SCR, PAD = 1920, 1080, 960, 60
WHITE, MID, FAINT = (255, 255, 255), (150, 150, 150), (72, 72, 72)   # (ui.c's mono)


def _font(size, bold):
    from PIL import ImageFont
    try:
        return ImageFont.truetype(FONT, size, index=1 if bold else 0)
    except OSError:
        return ImageFont.load_default(size)


def _wrap(d, text, f, width):
    lines, cur = [], ""
    for w in text.split(" "):                   # (a no-break space keeps words together)
        if cur and d.textlength(cur + " " + w, font=f) > width:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    return lines + [cur] if cur else lines


def caption_png(path, chapter, title, body, how, footer):
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (W - SCR - 3 * PAD, SCR), (0, 0, 0, 0))
    d, wd = ImageDraw.Draw(img), W - SCR - 3 * PAD
    d.text((0, 0), chapter.upper(), font=_font(28, False), fill=MID)
    y = 150
    for ln in _wrap(d, title, _font(64, True), wd):
        d.text((0, y), ln, font=_font(64, True), fill=WHITE)
        y += 76
    y += 18
    for ln in _wrap(d, body, _font(36, False), wd):
        d.text((0, y), ln, font=_font(36, False), fill=WHITE)
        y += 50
    if how:
        y = max(y + 40, 560)
        d.text((0, y), "ON THE FM-1", font=_font(24, True), fill=MID)
        y += 40
        for ln in _wrap(d, "  \u203a  ".join(how), _font(30, False), wd):
            d.text((0, y), ln, font=_font(30, False), fill=MID)
            y += 42
    d.text((0, SCR - 40), footer, font=_font(26, False), fill=FAINT)
    img.save(path)


def ffmpeg_exe():
    f = shutil.which("ffmpeg")
    if f: return f
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        sys.exit("no ffmpeg: brew install ffmpeg, or pip install imageio-ffmpeg")


def _ts(s, srt=True):
    ms = round(s * 1000)
    if srt:
        return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"
    s = ms // 1000
    return f"{s // 60}:{s % 60:02d}"


def film(scenes, name, footer, only=(), fps=60, ver="0.8"):
    """Film the scenes (only: just these, the others as filmed before) and join them: takes/NAME/NAME.mp4, .wav,
    .srt, chapters.txt."""
    import capture
    out = ROOT / "takes" / name
    out.mkdir(parents=True, exist_ok=True)
    todo = [s for s in scenes if not only or s.name in only]
    for s in todo:
        (out / s.name).mkdir(exist_ok=True)
    gen_states([(str(out / s.name / "state.bin"), *s.start) for s in todo])
    ff, parts, srt, chapters, at = ffmpeg_exe(), [], [], [], 0.0
    for s in scenes:
        s.done()
        d = out / s.name
        lead = (capture.LEAD_MS + (capture.SPLASH_MS if s.splash else 0)) / 1000
        last = max([t for t, _ in s.ev] + [s.t])
        dur = lead + last / 1000 + s.tail
        caps = [(0 if i == 0 else lead + t / 1000, title, body, how) for i, (t, title, body, how) in enumerate(s.caps)]
        if s in todo:
            lines = [f"# {s.name}: scripted by tools/film.py"] + [f"{t} {e}" for t, e in sorted(s.ev, key=lambda x: x[0])]
            (d / "events.txt").write_text("\n".join(lines + [f"{last} last"]) + "\n")
            capture.render(d, fps, SCR, s.tail, s.splash, ver)
            ins = ["-i", str(out / f"{s.name}.mp4")]
            chain = [f"color=c=black:s={W}x{H}:r={fps}:d={dur:.3f}[base]", f"[base][0:v]overlay={PAD}:{PAD}:shortest=1[v0]"]
            lbl = "v0"
            for i, (t, title, body, how) in enumerate(caps):
                png = d / f"cap{i}.png"
                caption_png(png, s.chapter, title, body, how, footer)
                ins += ["-i", str(png)]
                end = caps[i + 1][0] if i + 1 < len(caps) else dur + 1
                chain.append(f"[{lbl}][{i + 1}:v]overlay={SCR + 2 * PAD}:{PAD}:enable='between(t,{t:.3f},{end:.3f})'[v{i + 1}]")
                lbl = f"v{i + 1}"
            chain.append(f"[{lbl}]fade=t=in:d=0.3,fade=t=out:st={dur - 0.4:.3f}:d=0.4,format=yuv420p[v]")
            peak = subprocess.run([ff, "-i", str(d / "audio.wav"), "-af", "volumedetect", "-f", "null", "-"],
                                  capture_output=True, text=True).stderr.split("max_volume: ")[1].split(" dB")[0]
            chain.append(f"[0:a]volume={min(12.0, -3 - float(peak)):.2f}dB,afade=t=out:st={dur - 0.4:.3f}:d=0.4[a]")
            subprocess.run([ff, "-y", "-loglevel", "error", *ins, "-filter_complex", ";".join(chain), "-map", "[v]",
                            "-map", "[a]", "-r", str(fps), "-c:v", "libx264", "-crf", "16", "-c:a", "aac", "-b:a",
                            "256k", "-ar", "44100", "-t", f"{dur:.3f}", str(d / "framed.mp4")], check=True)
            print(f"-> {d / 'framed.mp4'} ({dur:.1f} s)")
        parts.append(d / "framed.mp4")
        if not chapters or chapters[-1][1] != s.chapter:
            chapters.append((at, s.chapter))
        for i, (t, title, body, _) in enumerate(caps):
            a, b = at + t, at + (caps[i + 1][0] if i + 1 < len(caps) else dur)
            srt.append(f"{len(srt) + 1}\n{_ts(a)} --> {_ts(b)}\n{title}\n{body}\n")
        at += dur
    if missing := [p for p in parts if not p.exists()]:
        print(f"(not joined: {len(missing)} scenes not filmed yet, e.g. {missing[0].parent.name})")
        return
    (out / "parts.txt").write_text("".join(f"file '{p}'\n" for p in parts))
    joined = out / "joined.mp4"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(out / "parts.txt"),
                    "-c", "copy", str(joined)], check=True)
    peak = subprocess.run([ff, "-i", str(joined), "-af", "volumedetect", "-f", "null", "-"], capture_output=True,
                          text=True).stderr.split("max_volume: ")[1].split(" dB")[0]
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(joined), "-c:v", "copy", "-af",
                    f"volume={-1 - float(peak):.2f}dB", "-c:a", "aac", "-b:a", "256k", str(out / f"{name}.mp4")],
                   check=True)
    joined.unlink()
    (out / f"{name}.srt").write_text("\n".join(srt))
    (out / "chapters.txt").write_text("".join(f"{_ts(t, False)} {c}\n" for t, c in chapters))
    print(f"-> {out / (name + '.mp4')} ({_ts(at, False)}), {name}.srt, chapters.txt")
