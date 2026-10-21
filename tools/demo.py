#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The demo video's sounds (docs: the Prysm demo video script), sent to the FM-1's patch slots.

    tools/demo.py send                  the 8 slots below -> the FM-1 (asks first: they replace what is there)
    tools/demo.py send --load 2         ... then slot 2 becomes the current sound
    tools/demo.py list                  what each slot holds
    tools/demo.py film                  the demo video, no FM-1 needed: takes/demo/prysm-demo.mp4 (1920 x 1080), .wav
                                        (the sound alone) and .srt (the captions, timed, for a voice-over)
    tools/demo.py film 07-arp           film only that scene again (the others as filmed before), then join them
    Options: --fps 60, --ver 0.8 (the splash's version), --no-captions (the screen alone)

The film's scenes (scenes() below) are scripted as a player plays them: page buttons, SELECT, knob turns, notes;
each is played through the firmware's own code (tools/capture.py render), framed with its captions and joined.

A patch is written as patch_t's fields over the default sound (PATCH_INIT, engine.c), in C: the tool compiles them
against the firmware's own sources (as tests/host.c does), so the bytes are exactly the firmware's patch_t. They go
over USB-MIDI as SysEx F0 7D 50 (capture.c: 'P' 64 bytes at a time, 'W' into a slot, 'L' load), each answered by the
FM-1. Needs firmware 0.8 or later, mido + python-rtmidi and a C compiler."""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

RATIOS = ["0.5", "1", "1.5", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"]
BANKS = ["BASIC", "HARM", "PULSE", "VOCAL", "DIGI", "CMPLX"]


def r(x):                                       # a ratio by its name: r("1.5")
    return RATIOS.index(str(x))


def w(i):                                       # wave i (0..7) of a bank: its position on the WAVE knob
    return round(i * 127 / 7)


def seq(notes, accents=(), slides=(), rests=()):   # a pattern: notes (MIDI), steps (0-based) accented / sliding / rests
    f = {"seq_len": len(notes)}
    for i, n in enumerate(notes):
        f[f"seq_note[{i}]"] = n
        f[f"seq_trig[{i}]"] = "SEQ_REST" if i in rests else "SEQ_ACCENT" if i in accents else "SEQ_NOTE"
        f[f"seq_slide[{i}]"] = int(i in slides)
    return f


# the slots, 1 .. 8: (name, fields changed from PATCH_INIT). Slot 8 stays empty: the finale saves into it.
PATCHES = [
    ("SINE", {                                  # scene 2: a bare sine, for the FM build-up
        "osc[0].amt[0]": 0, "osc[0].amt[1]": 0, "osc[0].ratio[1]": r(1), "lvl2": 0,
        "cut": 127, "res": 0, "fenv": 64, "amp_a": 2, "amp_d": 80, "amp_s": 127, "amp_r": 50,
    }),
    ("FM KEYS", {                               # scene 3: an electric piano, a tine on osc 2
        "osc[0].ratio[1]": r(1), "osc[0].amt[0]": 55, "osc[0].att[0]": 0, "osc[0].rel[0]": 55,
        "osc[1].pos[0]": w(0), "osc[1].ratio[1]": r(14), "osc[1].amt[0]": 35, "osc[1].rel[0]": 25,
        "lvl1": 110, "lvl2": 45, "fine2": 64,
        "cut": 112, "res": 10, "fenv": 64,
        "amp_a": 0, "amp_d": 98, "amp_s": 30, "amp_r": 62,
        "cho_mix": 70, "cho_rate": 35, "cho_depth": 55, "rev_mix": 20,
    }),
    ("BELLS", {                                 # scene 3: FM bells, echoes climbing an octave
        "osc[0].ratio[1]": r(3), "osc[0].amt[0]": 78, "osc[0].rel[0]": 108,
        "osc[1].pos[0]": w(0), "osc[1].ratio[1]": r(7), "osc[1].amt[0]": 40, "osc[1].rel[0]": 90,
        "lvl1": 110, "lvl2": 50, "fine2": 66,
        "cut": 120, "res": 0, "fenv": 64,
        "amp_a": 0, "amp_d": 112, "amp_s": 0, "amp_r": 108,
        "dly_mix": 50, "dly_time": 70, "dly_fb": 62, "dly_shift": 24, "dly_tone": 40,
        "rev_mix": 35, "rev_size": 100,
    }),
    ("ACID", {                                  # scenes 4 and 7: saw + square an octave down, LP 24, a 303 line
        "osc[0].pos[0]": w(4), "osc[0].amt[0]": 0,
        "osc[1].pos[0]": w(5), "osc[1].amt[0]": 0, "semi2": 12, "fine2": 64,
        "lvl1": 110, "lvl2": 75,
        "flt_type": "FLT_LP24", "cut": 42, "res": 100, "fenv": 100, "key": 64, "drive": 50,
        "flt_a": 0, "flt_d": 52, "flt_s": 0, "flt_r": 30,
        "amp_a": 0, "amp_d": 80, "amp_s": 100, "amp_r": 20,
        "vel_amp": 40, "vel_cut": 90, "vel_mod": 0,
        "glide": 30, "glide_mode": "GLIDE_LEGATO", "arp_bpm": 80,   # 120 BPM
        "seq_div": "DIV_16", "seq_gate": 80, "dly_mix": 25, "dly_time": 75, "dly_fb": 45,
        **seq([36, 36, 48, 36, 39, 36, 46, 36, 36, 43, 36, 48, 41, 36, 39, 46],
              accents=(2, 6, 10, 14), slides=(3, 12), rests=(7, 11)),
    }),
    ("COMB", {                                  # scene 4: the comb as a plucked string
        "osc[0].pos[0]": w(4), "osc[0].amt[0]": 90, "osc[0].rel[0]": 12, "lvl2": 0,
        "flt_type": "FLT_COMBP", "cut": 64, "key": 127, "res": 115, "fenv": 64,
        "comb_damp": 40, "comb_spread": 0,
        "amp_a": 0, "amp_d": 60, "amp_s": 0, "amp_r": 100,
        "rev_mix": 25,
    }),
    ("PAD", {                                   # scenes 6 and 8: vowels that morph, a shimmering plate
        "osc[0].bank[0]": BANKS.index("VOCAL"), "osc[0].pos[0]": 40, "osc[0].amt[0]": 20,
        "osc[1].bank[0]": BANKS.index("VOCAL"), "osc[1].pos[0]": 90, "fine2": 72,
        "lvl1": 100, "lvl2": 90,
        "cut": 90, "res": 20, "fenv": 64,
        "amp_a": 90, "amp_d": 90, "amp_s": 120, "amp_r": 105,
        "lfo_wave": "LFO_TRI", "lfo_rate": 30, "lfo_retrig": 0,
        "lfo_dest[0]": "DEST_WAVE1", "lfo_amt[0]": 100, "lfo_dest[1]": "DEST_WAVE2", "lfo_amt[1]": 34,
        "menv_a": 110, "menv_s": 127, "menv_dest": "DEST_LFOAMT", "menv_amt": 110,
        "cho_mix": 50, "rev_mix": 70, "rev_size": 115, "rev_shimmer": 50,
    }),
    ("BLEEPS", {                                # scene 5: S&H FM blips, arpeggiated, ping-pong in fourths
        "osc[0].ratio[1]": r(3), "osc[0].amt[0]": 80, "osc[0].rel[0]": 40, "lvl2": 0,
        "cut": 115, "res": 20, "fenv": 64,
        "amp_a": 0, "amp_d": 50, "amp_s": 0, "amp_r": 40,
        "lfo_wave": "LFO_SH", "lfo_rate": 70, "lfo_dest[0]": "DEST_MPITCH", "lfo_amt[0]": 100,
        "lfo_dest[1]": "DEST_OFF",
        "menv_loop": 1, "menv_a": 50, "menv_d": 60, "menv_s": 0, "menv_dest": "DEST_LFORATE", "menv_amt": 100,
        "dly_mix": 55, "dly_ping": 1, "dly_shift": 17, "dly_fb": 70, "dly_time": 60,
        "arp_mode": "ARP_UPDN", "arp_div": "DIV_16", "arp_oct": 1, "arp_bpm": 80, "arp_gate": 50,
        "arp_latch": 1,
    }),
]


def gen(states=()):
    """Compiled against the firmware: [(name, bytes)] for the slots, sizeof(patch_t), PATCH_VERSION; and for each
    (path, slot, page, rows) in states, a take's start state (capture.c's cap_state) written to path: the slots
    above, the slot's sound current, that page on screen, rows {page: row}."""
    cc = shutil.which("cc") or sys.exit("no C compiler (cc)")
    body = []
    for i, (_, fields) in enumerate(PATCHES):
        body.append(f"    p = PATCH_INIT;")
        body += [f"    p.{k} = {v};" for k, v in fields.items()]
        body.append(f"    fwrite(&p, sizeof p, 1, f);\n    bank.p[{i}] = p;\n    bank.used[{i}] = 1;")
    for path, slot, pg, rows in states:
        body.append(f"    edit = clean = bank.p[{slot}];\n    cur_slot = {slot};\n    gset.slot = {slot};\n"
                    f"    page = {pg};\n    memset(row, 0, sizeof row);\n    pa_slot = {slot};\n    pa_row = 0;")
        body += [f"    row[{p}] = {n};" for p, n in rows.items()]
        body.append(f"    n = cap_state(cap_buf, 0);\n    g = fopen(\"{path}\", \"wb\");\n"
                    f"    fwrite(cap_buf, 1, n, g);\n    fclose(g);")
    src = ("#define main prysm_host_main\n#include \"prysm.c\"\n#undef main\n"
           "int main(int argc, char **argv)\n{\n    FILE *f = fopen(argv[1], \"wb\"), *g;\n    patch_t p;\n"
           "    uint32_t n, h[2] = {sizeof p, PATCH_VERSION};\n    bank.version = PATCH_VERSION;\n"
           "    fwrite(h, 4, 2, f);\n" + "\n".join(body) + "\n    fclose(f);\n    return 0;\n}\n")
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "demo_gen.c").write_text(src)
        subprocess.run([cc, "-O0", "-w", "-DDINGHY_HOST", f"-I{ROOT / 'firmware/src'}", f"-I{ROOT / 'firmware/gen'}",
                        "-o", str(t / "gen"), str(t / "demo_gen.c")], check=True)
        subprocess.run([str(t / "gen"), str(t / "patches.bin")], check=True)
        b = (t / "patches.bin").read_bytes()
    size, version = struct.unpack("<II", b[:8])
    out = [(name, b[8 + i * size:8 + (i + 1) * size]) for i, (name, _) in enumerate(PATCHES)]
    return out, size, version


def patch_bytes():
    return gen()


# ---- the film: each scene a scripted take (what a player would press, turn and play), played through the firmware's
# own code on this computer (tools/capture.py render), then framed with its captions and joined

PAGES = {"OSC1": ["WAVE", "WAVE2", "RATIO", "MODE", "ENV", "MIX"], "OSC2": ["WAVE", "WAVE2", "RATIO", "MODE", "ENV", "MIX"],
         "FILTER": ["FILTER", "ENV", "TYPE", "COMB"], "AMP": ["ENV", "VEL"], "LFO": ["LFO", "DEST", "M.ENV", "M.DST"],
         "LFO2": ["LFO", "DEST"], "MIX": ["OSC", "GLIDE"], "ARP": ["ARP", "TIME"], "SEQ": ["STEP", "PATTERN"],
         "CHO": ["CHORUS"], "DLY": ["DELAY", "D.MOD"], "REV": ["REVERB", "R.MOD"], "SETUP": ["MIDI"],
         "PATCH": [], "SCOPE": []}
PG = {p: i for i, p in enumerate(PAGES)}        # (as ui.c numbers them: PG_OSC1 .. PG_SETUP, PG_PATCH, PG_SCOPE)
BTN = {"FX": 2, "SEL": 3, "ENV": 4, "LFO": 5, "EDIT": 6, "GLO": 7, "HOME": 8, "SAVE": 9, "ARP": 10, "SEQ": 11,
       "PLAY": 12, "REC": 13}
ENC_SELECT, ENC_KNOB1, ENC_PRESETS = 0, 2, 6
TURN_MS = 70                                    # between knob events: slower than the UI's 60 ms fast-turn (x3)


class Scene:
    """A take, written as a player plays it; times in ms from the take's start (t: where the next action goes)."""

    def __init__(self, name, slot, page="OSC1", rows=None, splash=False, tail=3.0):
        self.name, self.slot, self.page, self.splash, self.tail = name, slot - 1, page, splash, tail
        self.rows = {p: 0 for p in PAGES}
        self.rows.update({p: PAGES[p].index(r) for p, r in (rows or {}).items()})
        self.start = (slot - 1, PG[page], {PG[p]: n for p, n in self.rows.items() if n})
        self.t, self.ev, self.caps, self.back = 0, [], [], "OSC1"
        self.arp_on = PATCHES[slot - 1][1].get("arp_on", 0)

    def wait(self, ms):
        self.t += ms
        return self

    def caption(self, title, line=""):          # from now until the next one (or the end)
        self.caps.append((self.t, title, line))
        return self

    def press(self, b, gap=350):
        self.ev.append((self.t, f"btn {BTN[b]}"))
        p = self.page                           # (where it leads, as ui_input has it)
        if b == "HOME":
            self.page = self.back if p == "SCOPE" else p if p == "PATCH" else "SCOPE"
            if p not in ("SCOPE", "PATCH"): self.back = p
        elif b == "SAVE":
            if p not in ("PATCH",):
                if p != "SCOPE": self.back = p
                self.page = "PATCH"
        elif b == "EDIT": self.page = "OSC2" if p == "OSC1" else "OSC1"
        elif b == "FX": self.page = {"CHO": "DLY", "DLY": "REV"}.get(p, "CHO")
        elif b == "ENV": self.page = "FILTER" if p == "AMP" else "AMP"
        elif b == "SEL": self.page = "FILTER"
        elif b == "LFO": self.page = "LFO2" if p == "LFO" else "LFO"
        elif b == "GLO": self.page = "SETUP" if p == "MIX" else "MIX"
        elif b == "SEQ" or b == "REC": self.page = "SEQ"
        elif b == "ARP":
            if p == "ARP": self.arp_on ^= 1
            else: self.page = "ARP"
        return self.wait(gap)

    def goto(self, page):                       # by the page buttons, as a player would
        way = {"OSC1": "EDIT", "OSC2": "EDIT", "FILTER": "SEL", "AMP": "ENV", "LFO": "LFO", "LFO2": "LFO",
               "MIX": "GLO", "SETUP": "GLO", "ARP": "ARP", "SEQ": "SEQ", "CHO": "FX", "DLY": "FX", "REV": "FX",
               "SCOPE": "HOME", "PATCH": "SAVE"}[page]
        for _ in range(4):
            if self.page == page: return self
            self.press(way)
        sys.exit(f"{self.name}: no way to {page}")

    def row(self, name):                        # SELECT: the row
        d = PAGES[self.page].index(name) - self.rows[self.page]
        if d:
            self.ev.append((self.t, f"enc {ENC_SELECT} {d}"))
            self.rows[self.page] += d
            self.wait(400)
        return self

    def turn(self, knob, detents, ms=600, block=True):   # knob 1..4, or "cut" (PRESETS); spread over ms
        enc = ENC_PRESETS if knob == "cut" else ENC_KNOB1 + knob - 1
        n = max(1, min(abs(detents), ms // TURN_MS))
        done, t = 0, self.t
        for i in range(n):
            d = round(detents * (i + 1) / n) - done
            if d: self.ev.append((t, f"enc {enc} {d}"))
            done += d
            t += TURN_MS
        if block: self.t = t + 200
        return self

    def step(self, knob, steps, gap=900):       # a stepped knob (modes, LFO waves, on / off): two detents a step
        for _ in range(abs(steps)):
            self.ev.append((self.t, f"enc {ENC_KNOB1 + knob - 1} {2 if steps > 0 else -2}"))
            self.wait(gap)
        return self

    def on(self, *notes, vel=127):
        self.ev += [(self.t, f"midi 0x90 {n} {vel}") for n in notes]
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


def scenes():
    S = []
    s = Scene("01-intro", 6, splash=True, tail=4)
    s.caption("Prysm", "A 4-voice wavetable FM synthesizer for the M-VAVE FM-1.")
    s.wait(300).on(48, 55, 63, 70).wait(4500).off(48, 55, 63, 70)
    S.append(s)

    s = Scene("02-fm", 1, rows={"OSC1": "MIX"})
    s.caption("Three operators", "Each oscillator: a carrier, modulated by mod\u00a01, modulated by mod\u00a02.")
    s.wait(300).on(57).wait(1200)
    s.turn(1, 70, 3000).wait(600).turn(2, 45, 2200).wait(800)
    s.caption("Ratios", "Whole numbers sound like instruments; 1.5 and 0.5, hollow and sub.")
    s.row("RATIO").turn(2, 1, 100).wait(900).turn(2, 1, 100).wait(900).turn(2, 1, 100).wait(1200)
    s.caption("FM, ring or mix", "How each modulator acts on the wave it points at.")
    s.row("MODE").step(1, 1, 1500).step(1, 1, 1500).step(1, -2, 1200)
    s.off(57)
    S.append(s)

    s = Scene("03-keys", 2)
    s.caption("Electric piano", "Sine on sine, a tine on osc 2, and chorus.")
    s.wait(300).play([(50, 57, 60, 64), (55, 59, 62, 65), (48, 55, 59, 64)], dur=1300, gap=1450)
    s.play([(53, 57, 60, 64)], dur=2200, gap=2400)
    S.append(s)

    s = Scene("04-bells", 3, page="DLY", tail=5)
    s.caption("Bells", "FM bells. Each echo climbs an octave.")
    s.wait(300).play([72, 79, 76, 84], dur=200, gap=450).wait(2500)
    S.append(s)

    s = Scene("05-filter", 4, page="FILTER")
    s.caption("The filter", "12 or 24 dB low-pass with drive. It draws its own response.")
    s.wait(300)
    s.play([36, 36, 48, 36] * 6, dur=110, gap=250, at=s.t)
    s.turn("cut", 45, 3000).turn("cut", -30, 2500)
    s.row("TYPE").step(1, -1, 1500).step(1, 1, 1500)
    S.append(s)

    s = Scene("06-comb", 5, page="FILTER", rows={"FILTER": "COMB"}, tail=4)
    s.caption("A comb filter", "Plucked strings from the same filter. SPREAD turns them to bells and metal.")
    s.wait(300).play([48, 55, 60, 64, 67], dur=150, gap=420)
    s.play([60, 67, 72, 64, 60, 67, 72, 76], dur=150, gap=420, at=s.t)
    s.turn(2, 90, 3300)
    S.append(s)

    s = Scene("07-arp", 7, page="ARP")
    s.caption("Arpeggiator", "Up, down, random or as played; swing, latch, up to four octaves.")
    s.wait(300).press("ARP", 200).on(60, 63, 67, 70).wait(400).off(60, 63, 67, 70).wait(2600)
    s.caption("Swing and gate", "From straight to a triplet feel; at TIE the notes join.")
    s.row("TIME").turn(3, 87, 1500).wait(2500).turn(2, 80, 1500).wait(2500)
    S.append(s)

    s = Scene("08-pad", 6, page="LFO", rows={"LFO": "DEST"}, tail=7)
    s.caption("Wavetables that morph", "Vowels sweeping through their bank, moved by the LFO.")
    s.wait(300).on(48, 55, 63, 70).wait(4500)
    s.caption("Plate reverb", "With modulation and shimmer: the tail climbs in octaves.")
    s.goto("REV").turn(1, 10, 700).row("R.MOD").turn(3, 60, 2400).wait(2500)
    s.off(48, 55, 63, 70)
    S.append(s)

    s = Scene("09-seq", 4, page="SEQ")
    s.caption("Sequencer", "303-style: 64 steps with accents and slides, entered from the keys.")
    s.wait(300).press("PLAY", 3700)
    s.turn("cut", 40, 4000).wait(1500).turn("cut", -25, 2000).wait(800)
    s.press("PLAY")
    S.append(s)

    s = Scene("10-scope", 2)
    s.caption("Oscilloscope", "Locked onto the newest note, zooming by itself.")
    s.wait(300).press("HOME", 300).on(57).wait(1800).turn(1, 2, 300).wait(1500).off(57)
    s.on(64).wait(1800).off(64)
    S.append(s)

    s = Scene("11-end", 6, tail=6)
    s.caption("8 patch slots", "Saved in flash. USB audio and MIDI built in.")
    s.wait(300).on(48, 55, 63, 70).wait(1200).press("SAVE", 900).turn(1, 2, 300).wait(700).press("SAVE", 1500)
    s.caption("Free and open source", "Install from your browser: nicholaspratt.github.io/Prysm")
    s.wait(3000).off(48, 55, 63, 70)
    S.append(s)
    return S


FONT = "/System/Library/Fonts/HelveticaNeue.ttc"
W, H, SCR, PAD = 1920, 1080, 960, 60            # the frame; the screen (4 x 240) at PAD, PAD; captions to its right
WHITE, MID, FAINT = (255, 255, 255), (150, 150, 150), (72, 72, 72)   # (ui.c's mono: C_BRIGHT, C_MID, C_FAINT)


def caption_png(path, title, line, n, total):
    from PIL import Image, ImageDraw, ImageFont

    def font(size, bold):
        try:
            return ImageFont.truetype(FONT, size, index=1 if bold else 0)
        except OSError:
            return ImageFont.load_default(size)
    img = Image.new("RGBA", (W - SCR - 3 * PAD, SCR), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.text((0, 0), f"{n:02d} / {total:02d}", font=font(30, False), fill=FAINT)
    d.text((0, 300), title, font=font(76, True), fill=WHITE)
    y, words, cur, f = 410, line.split(" "), "", font(38, False)   # (a no-break space keeps words together)
    for wd in words + [None]:                   # (wrapped to the column)
        if wd is not None and d.textlength((cur + " " + wd).strip(), font=f) <= img.width:
            cur = (cur + " " + wd).strip()
            continue
        d.text((0, y), cur, font=f, fill=MID)
        y, cur = y + 54, wd or ""
    d.text((0, SCR - 40), "PRYSM  ·  firmware for the M-VAVE FM-1", font=font(26, False), fill=FAINT)
    img.save(path)


def ffmpeg_exe():
    f = shutil.which("ffmpeg")
    if f: return f
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        sys.exit("no ffmpeg: brew install ffmpeg, or pip install imageio-ffmpeg")


def srt_time(s):
    ms = round(s * 1000)
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def film(only, fps, ver, captions):
    sys.path.insert(0, str(ROOT / "tools"))
    import capture
    out = ROOT / "takes" / "demo"
    out.mkdir(parents=True, exist_ok=True)
    all_s = scenes()
    todo = [s for s in all_s if not only or s.name in only]
    gen([(str(out / s.name / "state.bin"), *s.start) for s in todo if (out / s.name).mkdir(exist_ok=True) or True])
    ff, parts, srt, at = ffmpeg_exe(), [], [], 0.0
    for s in all_s:
        d = out / s.name
        lead = (capture.LEAD_MS + (capture.SPLASH_MS if s.splash else 0)) / 1000
        last = max([t for t, _ in s.ev] + [s.t])
        dur = lead + last / 1000 + s.tail
        if s in todo:
            lines = [f"# {s.name}: scripted by tools/demo.py"] + [f"{t} {e}" for t, e in sorted(s.ev, key=lambda x: x[0])]
            (d / "events.txt").write_text("\n".join(lines + [f"{last} last"]) + "\n")
            capture.render(d, fps, SCR, s.tail, s.splash, ver)
            caps = [(0 if i == 0 else lead + t / 1000, title, line) for i, (t, title, line) in enumerate(s.caps)]
            ins, chain, last_lbl = ["-i", str(out / f"{s.name}.mp4")], [], "base"
            chain.append(f"color=c=black:s={W}x{H}:r={fps}:d={dur:.3f}[base]")
            chain.append(f"[base][0:v]overlay={PAD}:{PAD}:shortest=1[v0]")
            last_lbl = "v0"
            if captions:
                for i, (t, title, line) in enumerate(caps):
                    png = d / f"cap{i}.png"
                    caption_png(png, title, line, all_s.index(s) + 1, len(all_s))
                    ins += ["-i", str(png)]
                    end = caps[i + 1][0] if i + 1 < len(caps) else dur + 1
                    chain.append(f"[{last_lbl}][{i + 1}:v]overlay={SCR + 2 * PAD}:{PAD}:"
                                 f"enable='between(t,{t:.3f},{end:.3f})'[v{i + 1}]")
                    last_lbl = f"v{i + 1}"
            chain.append(f"[{last_lbl}]fade=t=in:d=0.3,fade=t=out:st={dur - 0.4:.3f}:d=0.4,format=yuv420p[v]")
            peak = subprocess.run([ff, "-i", str(d / "audio.wav"), "-af", "volumedetect", "-f", "null", "-"],
                                  capture_output=True, text=True).stderr.split("max_volume: ")[1].split(" dB")[0]
            chain.append(f"[0:a]volume={-3 - float(peak):.2f}dB,afade=t=out:st={dur - 0.4:.3f}:d=0.4[a]")   # (each scene's peak at -3 dB)
            subprocess.run([ff, "-y", "-loglevel", "error", *ins, "-filter_complex", ";".join(chain),
                            "-map", "[v]", "-map", "[a]", "-r", str(fps), "-c:v", "libx264", "-crf", "16",
                            "-c:a", "aac", "-b:a", "256k", "-ar", "44100", "-t", f"{dur:.3f}", str(d / "framed.mp4")],
                           check=True)
            print(f"-> {d / 'framed.mp4'} ({dur:.1f} s)")
        parts.append(d / "framed.mp4")
        for i, (t, title, line) in enumerate(s.caps):
            a = at + (0 if i == 0 else lead + t / 1000)
            b = at + (lead + s.caps[i + 1][0] / 1000 if i + 1 < len(s.caps) else dur)
            srt.append(f"{len(srt) + 1}\n{srt_time(a)} --> {srt_time(b)}\n{title}: {line}\n")
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
                    f"volume={-1 - float(peak):.2f}dB", "-c:a", "aac", "-b:a", "256k", str(out / "prysm-demo.mp4")],
                   check=True)                  # (the loudest moment at -1 dB)
    joined.unlink()
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(out / "prysm-demo.mp4"), "-vn", "-c:a", "pcm_s16le",
                    str(out / "prysm-demo.wav")], check=True)
    (out / "prysm-demo.srt").write_text("\n".join(srt))
    print(f"-> {out / 'prysm-demo.mp4'} ({at:.1f} s), prysm-demo.wav (the sound alone), "
          f"prysm-demo.srt (the captions, timed: for the voice-over)")


def find_port(names):
    for n in names:
        if "fm-1" in n.lower() or "prysm" in n.lower() or "felucca" in n.lower():
            return n
    sys.exit("no FM-1 among the MIDI ports: " + ", ".join(names))


def send(load):
    import mido
    patches, size, version = patch_bytes()
    inp_name, out_name = find_port(mido.get_input_names()), find_port(mido.get_output_names())
    print(f"FM-1 on {out_name}. Slots 1..{len(patches)} will be replaced by:")
    for i, (name, _) in enumerate(patches):
        print(f"  {i + 1}  {name}")
    if input("Send them? [y/N] ").strip().lower() != "y":
        sys.exit("nothing sent")
    with mido.open_input(inp_name) as inp, mido.open_output(out_name) as out:
        for m in inp.iter_pending():            # (what was waiting: not an answer)
            pass

        def ask(data, what):                    # one message, then its answer 'A' type i rc (3 tries)
            for _ in range(3):
                out.send(mido.Message("sysex", data=[0x7D, 0x50, *data]))
                t = time.time()
                while time.time() - t < 1.5:
                    for m in inp.iter_pending():
                        if (m.type == "sysex" and len(m.data) == 6 and m.data[:3] == (0x7D, 0x50, ord("A"))
                                and m.data[3] == data[0] and m.data[4] == data[1]):
                            return m.data[5]
                    time.sleep(0.002)
            sys.exit(f"{what}: no answer from the FM-1. Is it running Prysm 0.8 or later?")

        for s, (name, b) in enumerate(patches):
            for i in range(0, size, 64):
                chunk = b[i:i + 64].ljust(64, b"\0")
                ask([ord("P"), i // 64] + [x for v in chunk for x in (v >> 4, v & 15)], f"slot {s + 1}")
            rc = ask([ord("W"), s, size >> 7, size & 127, version], f"slot {s + 1}")
            if rc:
                sys.exit(f"slot {s + 1}: " + {1: "the FM-1's patch format differs: rebuild and install the "
                                                "firmware from this checkout", 2: "the save failed"}.get(rc, f"error {rc}"))
            print(f"  slot {s + 1}: {name} saved")
        if load:
            if ask([ord("L"), load - 1], f"load {load}"):
                sys.exit(f"slot {load} is empty")
            print(f"  slot {load} loaded")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["send", "list", "film"])
    ap.add_argument("scenes", nargs="*", help="film: only these scenes (e.g. 02-fm); the others as filmed before")
    ap.add_argument("--load", type=int, metavar="SLOT", help="send: then make this slot (1..8) the current sound")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--ver", default="0.8", help="film: the version the splash shows")
    ap.add_argument("--no-captions", action="store_true", help="film: the screen alone, for captions of your own")
    a = ap.parse_args()
    if a.what == "film":
        film(a.scenes, a.fps, a.ver, not a.no_captions)
    elif a.what == "list":
        _, size, version = patch_bytes()
        for i, (name, fields) in enumerate(PATCHES):
            print(f"{i + 1}  {name:8}  {len(fields)} fields changed from the default")
        print(f"(patch_t: {size} bytes, format {version})")
    else:
        send(a.load)


if __name__ == "__main__":
    main()
