#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What's new in Prysm 1.7, as a fast, noisy techno track (140 BPM, F minor) that is pushed until it breaks: the
ROUTE pin matrix, COMP (the drums pumping), the safety limiter, six-channel USB (stems pinned to U2 and U3), drum pan,
SYNC oscillators, delay self-oscillation, snare ghosts and rolls, then the Juno chorus for a quiet ending. The point
it makes on the way: Prysm is not for beginners.

The sound is DUAL: part A a distorted FM bass (OSC 1), part B a hard-synced lead (OSC 2, SYNC). Captions only. Each
scene picks up the sound where the last one left it.

film.py (the scene engine) is older than 1.7: its page list has no COMP or ROUTE, and HOME and FX go round fewer pages.
This film brings its tables up to date as it loads (the pages and rows from the firmware itself), without changing it.

    tools/films/whatsnew17.py               -> takes/whatsnew17/whatsnew17.mp4, .srt, chapters.txt
    tools/films/whatsnew17.py 03-route      film only that scene again (the others as filmed before), then join"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import film as F
from film import Seq, film, reading_ms


# ---- film.py, brought up to 1.7: pages, rows and stepped knobs read from the firmware

def _fw_tables():
    src = ('#define main prysm_host_main\n#include "prysm.c"\n#undef main\nint main(void)\n{\n    uint32_t p, r, k;\n'
           '    static const int s[] = {K_MODE, K_LFOW, K_ONOFF, K_ARPM, K_GLIDEM, K_CLK, K_HOME, K_STRIG, K_SSLIDE,\n'
           '        K_SMODE, K_SCOPY, K_SPASTE, K_SCLEAR, K_SINIT, K_NTYPE, K_ROUTE, K_DRUM, K_DHIT, K_DCLR, K_DROLL,\n'
           '        K_DRLD, K_DLANE, K_DCOPY, K_DPASTE, K_SLEARN, K_DPRE, K_DLOAD, K_SMUTE, K_SPCLR, K_SPOCT, K_DKIT,\n'
           '        K_LKDEST, K_MMSRC, K_MFX};\n'
           '    for (p = 0; p < PG_N; p++) {\n        printf("page %s", PAGES[p].name);\n'
           '        for (r = 0; r < PAGES[p].n; r++) printf("|%s", PAGES[p].rows[r].name);\n        printf("\\n");\n    }\n'
           '    printf("stepped");\n    for (k = 0; k < sizeof s / sizeof s[0]; k++) printf(" %d", s[k]);\n'
           '    for (k = 0; k < 256u; k++) if (loop_kind(k)) printf(" %u", k);\n    printf("\\n");\n    return 0;\n}\n')
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "t.c").write_text(src)
        subprocess.run([F.cc(), "-O0", "-w", "-DDINGHY_HOST", f"-I{F.ROOT / 'firmware/src'}",
                        f"-I{F.ROOT / 'firmware/gen'}", "-o", str(t / "t"), str(t / "t.c")], check=True)
        out = subprocess.run([str(t / "t")], capture_output=True, text=True, check=True).stdout.splitlines()
    pages = [ln[5:].split("|") for ln in out if ln.startswith("page ")]
    stepped = {int(x) for x in out[-1].split()[1:]}
    return pages, stepped


_KEY = {"OSC 1": "OSC1", "OSC 2": "OSC2", "F.ENV": "FENV", "M.ENV 1": "MENV", "M.ENV 2": "MENV2", "LFO 2": "LFO2",
        "CHORUS": "CHO", "DELAY": "DLY", "REVERB": "REV", "MOD FX": "MFX"}
_pages, _stepped = _fw_tables()
F.PAGES.clear()
for name, *rows in _pages:
    F.PAGES[_KEY.get(name, name)] = name
    F.ROWS[_KEY.get(name, name)] = rows
F.PAGES.update({"PATCH": "PATCH", "SCOPE": "SCOPE", "LIVE": "LIVE"})   # (ui.c: PG_N, PG_N + 1, PG_N + 2)
F.PG.clear()
F.PG.update({p: i for i, p in enumerate(F.PAGES)})
F.STEPPED = _stepped
RT_ROWS = ["OSC 1", "OSC 2", "NOISE", "KICK", "SNARE", "HAT", "CLAP", "FILT A", "FILT B", "CRUSH", "MOD FX", "CHORUS",
           "DELAY", "REVERB", "COMP"]
RT_COLS = ["FLT", "CRS", "MFX", "CHO", "DLY", "REV", "CMP", "OUT", "U2", "U3"]
RT_AT = None                                    # (patch_t's rt: its offset, from the knob table)


def rt_ok(r, c):
    if r < RT_ROWS.index("FILT A"): return True
    if r < RT_ROWS.index("CRUSH"): return c >= 1
    return c > 1 + (r - RT_ROWS.index("CRUSH"))


class Scene(F.Scene):
    """film.Scene, with 1.7's buttons (HOME: LIVE, SCOPE, ROUTE, back; FX: CRUSH .. COMP) and the ROUTE grid (pin)."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.rt_r, self.rt_c = 0, 0             # (the grid's cursor: OSC 1 > FLT as a take starts)

    def press(self, b, gap=500):
        p = self.page
        if b == "HOME":
            self.ev.append((self.t, f"btn {F.BTN[b]}"))
            self.page = {"LIVE": "SCOPE", "SCOPE": "ROUTE", "ROUTE": self.back, "PATCH": self.back}.get(p, "LIVE")
            if p not in ("SCOPE", "PATCH", "LIVE", "ROUTE"): self.back = p
            self._how("press HOME")
            return self.wait(gap)
        if b == "FX":
            self.ev.append((self.t, f"btn {F.BTN[b]}"))
            order = ["CRUSH", "MFX", "CHO", "DLY", "REV", "COMP"]
            self.page = order[order.index(p) + 1] if p in order[:-1] else "CRUSH"
            self._how("press FX")
            return self.wait(gap)
        return super().press(b, gap)

    def go(self, page):
        if page in ("CRUSH", "MFX", "CHO", "DLY", "REV", "COMP"):
            way = "FX"
        elif page in ("SCOPE", "LIVE", "ROUTE"):
            way = "HOME"
        else:
            return super().go(page)
        for _ in range(7):
            if self.page == page: return self
            self.press(way)
        sys.exit(f"{self.name}: no way to {page}")

    def _rt_shown(self, r):                     # (FILT B: with two parts alone)
        vmode = self.patch[F.info()["knobs"][("MIX", "PARTS", 0)][0]]
        return RT_ROWS[r] != "FILT B" or vmode != 0

    def pin(self, row, col, level, ms=900):
        """On the ROUTE screen: the cursor to row > col (KNOB1, KNOB2), then the pin to level (KNOB3; full or off with
        KNOB4, two detents), over about ms."""
        assert self.page == "ROUTE", self.page
        r, c = RT_ROWS.index(row), RT_COLS.index(col)
        assert rt_ok(r, c), (row, col)
        shown = [i for i in range(len(RT_ROWS)) if self._rt_shown(i)]
        d = shown.index(r) - shown.index(self.rt_r)
        if d:
            self.ev.append((self.t, f"enc {F.ENC_KNOB1} {d}"))
            self.rt_r = r
            self.rt_c = min((x for x in range(len(RT_COLS)) if rt_ok(r, x)), key=lambda x: (abs(x - self.rt_c), -x))   # (the firmware: right first)
            self._how(f"turn KNOB 1 to {row}")
            self.wait(300)
        if c != self.rt_c:
            self.ev.append((self.t, f"enc {F.ENC_KNOB1 + 1} {c - self.rt_c}"))
            self.rt_c = c
            self._how(f"turn KNOB 2 to {col}")
            self.wait(300)
        off = RT_AT + r * len(RT_COLS) + c
        cur = self.patch[off]
        if level in (0, 127) and cur != level:  # (KNOB4: all on, or off)
            d = 2 if level else -2
            self.ev.append((self.t, f"enc {F.ENC_KNOB1 + 3} {d}"))
            self._how("turn KNOB 4 (on / off)")
        elif level != cur:                      # (KNOB3, in steps of 10 or more: each as it is, never x 3)
            n = max(1, min(6, abs(level - cur) // 10))
            done = 0
            for i in range(n):
                step = round((level - cur) * (i + 1) / n) - done
                self.ev.append((self.t + i * max(80, ms // n), f"enc {F.ENC_KNOB1 + 2} {step}"))
                done += step
            self.t += (n - 1) * max(80, ms // n)
            self._how("turn KNOB 3 (the level)")
        self.patch[off] = level
        return self.wait(400)


def _rt_at():
    k = F.info()["knobs"]
    return k[("ROUTE", "OSC 1", 0)][0]          # (OSC 1 > FILTER: rt[0][0])


RT_AT = _rt_at()

NB = " "
KICK, SNARE, HAT, CLAP = range(4)
HIT, ACC, FLAM, GHOST = 1, 2, 3, 4
BPM, SWEET_BPM = 140, 84
STEP = 60000 / BPM / 4
PAT_KEY = [1, 3, 5, 8]                           # (LIVE: the first four black keys, P1 .. P4)
X1, X2, X3, X4 = 1, 3, 4, 5                      # (ratios: PR_RATIO_NAME's places)
SYNC, FM = 3, 0                                  # (a modulator's MODE)


def cpatch(base, lines):
    """A patch: base (bytes) with C run on it as the firmware would (`edit`)."""
    src = ('#define main prysm_host_main\n#include "prysm.c"\n#undef main\nint main(void)\n{\n    uint32_t i;\n'
           f'    static const uint8_t b[] = {{{",".join(str(x) for x in base)}}};\n    memcpy(&edit, b, sizeof edit);\n'
           + "".join(f"    {c}\n" for c in lines) +
           '    for (i = 0; i < sizeof edit; i++) printf("%u ", ((uint8_t *)&edit)[i]);\n    return 0;\n}\n')
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        (t / "p.c").write_text(src)
        subprocess.run([F.cc(), "-O0", "-w", "-DDINGHY_HOST", f"-I{F.ROOT / 'firmware/src'}",
                        f"-I{F.ROOT / 'firmware/gen'}", "-o", str(t / "p"), str(t / "p.c")], check=True)
        out = subprocess.run([str(t / "p")], capture_output=True, text=True, check=True).stdout
    return bytes(int(x) for x in out.split())


def knobs(base, kv):
    """base with knobs set: {(page, row, knob 1 .. 4[, "alt"]): value}; {("PIN", row, col): level} a ROUTE pin."""
    I = F.info()
    p = bytearray(base)
    for key, v in kv.items():
        if key[0] == "PIN":
            p[RT_AT + RT_ROWS.index(key[1]) * len(RT_COLS) + RT_COLS.index(key[2])] = v
            continue
        page, row, k, *alt = key
        p[I["knobs"][(page, row, k - 1, *alt)][0]] = v
    return bytes(p)


# ---- the sound: DUAL, part A an FM bass, part B a lead (FM first; SYNC in scene 4)

SHARED = {("MIX", "PARTS", 1): 1,               # DUAL
          ("ARP", "TIME", 1): BPM - 40,
          ("CHO", "CHORUS", 3): 0,
          ("DLY", "D.SYNC", 1): 1, ("DLY", "DELAY", 1): 43, ("DLY", "DELAY", 2): 72, ("DLY", "DELAY", 3): 62,
          ("DLY", "DELAY", 4): 1,
          ("REV", "REVERB", 1): 64, ("REV", "REVERB", 2): 80, ("REV", "REVERB", 3): 48, ("REV", "REVERB", 4): 6,
          ("COMP", "COMP", 1): 116, ("COMP", "COMP", 2): 8,   # (20:1)
          ("COMP", "COMP", 3): 127, ("COMP", "COMP", 4): 0,
          ("COMP", "C.TIME", 1): 6, ("COMP", "C.TIME", 2): 52,
          ("MIX", "LEVELS", 1): 127, ("MIX", "LEVELS", 2): 127, ("MIX", "LEVELS", 4): 76}
BASS = {("OSC1", "MOD1", 3): X1, ("OSC1", "MOD1", 4): 92, ("OSC1", "MOD2", 3): X2, ("OSC1", "MOD2", 4): 66,
        ("OSC1", "ENV1", 2): 34, ("OSC1", "ENV2", 2): 26,
        ("FILTER", "FILTER", 1): 64, ("FILTER", "FILTER", 2): 74, ("FILTER", "FILTER", 3): 104,
        ("FILTER", "TYPE", 2): 112,
        ("FENV", "ENV", 1): 0, ("FENV", "ENV", 2): 34, ("FENV", "ENV", 3): 0, ("FENV", "ENV", 4): 24,
        ("AMP", "ENV", 1): 0, ("AMP", "ENV", 2): 60, ("AMP", "ENV", 3): 110, ("AMP", "ENV", 4): 16}
LEAD = {("OSC2", "CAR", 1): 1, ("OSC2", "CAR", 2): 70, ("MIX", "OSC", 2): 127,
        ("OSC2", "MOD1", 3): X2, ("OSC2", "MOD1", 4): 58, ("OSC2", "ENV1", 1): 0, ("OSC2", "ENV1", 2): 46,
        ("OSC2", "MOD1", 3, "alt"): 96,         # (SYNC1 x4, waiting for SYNC)
        ("FILTER", "B FILTER", 1): 96, ("FILTER", "B FILTER", 2): 36, ("FILTER", "B TYPE", 2): 70,
        ("AMP", "B ENV", 1): 0, ("AMP", "B ENV", 2): 70, ("AMP", "B ENV", 3): 100, ("AMP", "B ENV", 4): 30}
START = knobs(F.info()["init"], {**SHARED, **BASS, **LEAD})
# (what the scenes do to it, for the takes that start further on)
ROUTE_DONE = {("PIN", "SNARE", "REV"): 96, ("PIN", "CLAP", "REV"): 64, ("PIN", "HAT", "DLY"): 64,
              ("PIN", "FILT B", "CRS"): 0, ("PIN", "FILT B", "DLY"): 127, ("PIN", "FILT B", "REV"): 127,
              ("PIN", "FILT B", "OUT"): 127}
COMP_DONE = {("PIN", "KICK", "CMP"): 127, ("PIN", "KICK", "OUT"): 0, ("PIN", "HAT", "CMP"): 127, ("PIN", "HAT", "OUT"): 0,
             ("PIN", "FILT A", "CRS"): 0, ("PIN", "FILT A", "CMP"): 127, ("COMP", "COMP", 1): 60,
             ("COMP", "COMP", 4): 15}
SYNC_DONE = {("OSC2", "ENV1", 3): SYNC, ("OSC2", "ENV1", 1): 95, ("OSC2", "ENV1", 2): 95, ("OSC2", "MOD1", 3, "alt"): 120, ("OSC2", "MOD1", 4): 96}
PAN_DONE = {("DRUM", "HAT", 4): 26, ("DRUM", "CLAP", 4): 100, ("DRUM", "SNARE", 4): 84}
FULL = knobs(START, {**ROUTE_DONE, **COMP_DONE, **SYNC_DONE, **PAN_DONE})

# the quiet one: a pad (part A) and a bell (part B), the Juno chorus, a long reverb
SWEET = knobs(F.info()["init"], {
    ("MIX", "PARTS", 1): 1, ("ARP", "TIME", 1): SWEET_BPM - 40,
    ("OSC1", "MOD1", 3): X2, ("OSC1", "MOD1", 4): 30, ("OSC1", "MOD2", 4): 0,
    ("FILTER", "FILTER", 1): 70, ("FILTER", "FILTER", 2): 10,
    ("AMP", "ENV", 1): 70, ("AMP", "ENV", 2): 90, ("AMP", "ENV", 3): 110, ("AMP", "ENV", 4): 84,
    ("OSC2", "MOD1", 3): X4, ("OSC2", "MOD1", 4): 44, ("OSC2", "ENV1", 2): 70,
    ("AMP", "B ENV", 1): 0, ("AMP", "B ENV", 2): 84, ("AMP", "B ENV", 3): 0, ("AMP", "B ENV", 4): 80,
    ("FILTER", "B FILTER", 1): 110,
    ("CHO", "CHORUS", 4): 3, ("CHO", "CHORUS", 2): 90, ("CHO", "CHORUS", 3): 100,
    ("DLY", "D.SYNC", 1): 1, ("DLY", "DELAY", 1): 75, ("DLY", "DELAY", 2): 56, ("DLY", "DELAY", 3): 34,
    ("DLY", "DELAY", 4): 1,
    ("REV", "REVERB", 1): 116, ("REV", "REVERB", 2): 60, ("REV", "REVERB", 3): 62, ("REV", "REVERB", 4): 20,
    ("REV", "R.MOD", 3): 40,
    ("MIX", "LEVELS", 1): 104, ("MIX", "LEVELS", 2): 92, ("MIX", "LEVELS", 3): 52, ("MIX", "LEVELS", 4): 80})


# ---- the music: F minor

F1, AB1, EB2, F2 = 29, 32, 39, 41
LEAD_P1 = [(3, 53, 1), (8, 56, 1), (11, 60, 2), (15, 63, 1)]   # (step, note, length): F3 Ab3 C4 Eb4
LEAD_OCT = -12                                   # (the lead: an octave down)
ACID = [53, 53, 56, 53, 60, 53, 63, 53, 65, 53, 63, 60, 56, 53, 51, 53]   # P2: sixteenths


def bass(q, p):
    line = [None, F1, F1, F2, None, F1, F1, EB2, None, F1, F1, F2, None, F1, AB1, F2]
    for k, n in enumerate(line, 1):
        if n: q.note(p, k, n, accent=k in (3, 11), slide=k == 15)


def drums(q, p, wild=False):
    q.drum(p, KICK, [1, 5, 9, 13], HIT).drum(p, KICK, [1], ACC)
    q.drum(p, CLAP, [5, 13], HIT)
    q.drum(p, HAT, [k for k in range(1, 17) if k % 4 != 3], HIT).drum(p, HAT, [3, 7, 11, 15], ACC)
    q.drum(p, SNARE, [8, 10, 16], GHOST)
    if wild:                                    # (rolls: the hat's, then the snare's into the bar line)
        q.raw(f"roll_put(&seqs.pat[{p}], 2, 3, 1);", f"roll_put(&seqs.pat[{p}], 2, 11, 3);",
              f"roll_put(&seqs.pat[{p}], 2, 14, 5);")
        q.drum(p, SNARE, [13, 14, 15], HIT).drum(p, SNARE, [16], FLAM)
        q.raw(f"roll_put(&seqs.pat[{p}], 1, 12, 1);", f"roll_put(&seqs.pat[{p}], 1, 13, 2);",
              f"roll_put(&seqs.pat[{p}], 1, 14, 4);")


def techno_seq(cur=0):
    """P1 the groove (the lead's stabs), P2 the chaos (the lead in sixteenths, rolls)."""
    q = Seq()
    bass(q, 0); drums(q, 0)
    for k, n, st in LEAD_P1:
        q.note(0, k, n + LEAD_OCT, lane=1, steps=st, part=1)
    bass(q, 1); drums(q, 1, wild=True)
    for k, n in enumerate(ACID, 1):
        q.note(1, k, n + LEAD_OCT, lane=1, part=1, accent=False, slide=k in (4, 12))
    q.raw(*[f"seqs.pat[{i}].len = 16;" for i in range(4)])
    q.raw("seqs.dr_kit = 0;", "seqs.dr_lvl[0] = 100;", "seqs.dr_lvl[1] = 72;", "seqs.dr_lvl[2] = 66;",
          "seqs.dr_lvl[3] = 80;", "seqs.dr_dec[0] = 110;", "seqs.dr_drive = 76;", "seqs.dr_crush = 18;",
          "seqs.dr_ghost = 40;", f"seqs.cur = {cur};")
    return q


DB3, F3, AB3, C4, EB3, C3, BB2, DB4, EB4, F4, AB4, BB3, G3 = 49, 53, 56, 60, 51, 48, 46, 61, 63, 65, 68, 58, 55
CHORDS = [(DB3, F3, AB3), (C3, EB3, AB3), (BB2, DB3, F3), (C3, EB3, G3)]   # Db, Ab/C, Bbm, C (Fm's V): home
BELL = [(1, C4), (4, AB3), (7, F3), (9, EB4), (13, DB4), (17, F4), (21, EB4), (25, C4), (33, DB4), (37, F3), (41, AB3),
        (49, C4), (53, EB4), (57, AB4)]


def sweet_seq():
    q = Seq()
    for b, ch in enumerate(CHORDS):
        for lane, n in enumerate(ch):
            q.note(0, 16 * b + 1, n, lane=lane, steps=15)
    for k, n in BELL:
        q.note(0, k, n, lane=3, steps=2, part=1)
    q.raw("seqs.pat[0].len = 64;", "seqs.dr_lvl[0] = seqs.dr_lvl[1] = seqs.dr_lvl[2] = seqs.dr_lvl[3] = 0;",
          "seqs.cur = 0;")
    return q


class Clock:
    """The bar lines of a sequencer started (PLAY) at t0."""

    def __init__(self, s, bpm=BPM):
        self.s, self.t0, self.step = s, s.t, 60000 / bpm / 4

    def at(self, bar, step=1):
        return self.t0 + round((16 * bar + step - 1) * self.step)

    def until(self, bar, step=1, early=0):
        self.s.t = max(self.s.t, self.at(bar, step) - early)
        return self.s

    def next_bar(self, early=0):
        n = 0
        while self.at(n) - early < self.s.t:
            n += 1
        return n

    def hold(self, early=0, extra=0):           # the caption read, then on to the next bar line (and extra bars)
        t0, a, b, _ = self.s.caps[-1]
        self.s.t = max(self.s.t, t0 + reading_ms(a, b))
        n = self.next_bar(early) + extra
        self.until(n, early=early)
        return n


def one(p, q):
    return {1: (bytes(p), q)}


def scenes():
    S = []

    # 1 ---- cold open: the whole thing, flat out, on the scope
    s = Scene("01-open", "1 · Prysm 1.7", tail=1, slots=one(knobs(FULL, {("CRUSH", "CRUSH", 1): 70}), techno_seq(cur=1)),
              load=1)
    s.go("SCOPE").press("PLAY", 0)
    c = Clock(s)
    s.wait(2600)
    s.caption("Prysm 1.7", "140 BPM. One M-VAVE FM-1. Nothing else plugged in.")
    c.hold(extra=1)
    s.caption("This is not a beginner's synth.",
              "No presets to hide behind, no guard rails on the sound. If you want easy, this isn't it.")
    c.hold(extra=1)
    s.press("PLAY", 0)
    S.append(s)

    # 2 ---- the ROUTE screen: every pin a level
    s = Scene("02-route", "2 · The ROUTE screen", tail=1, slots=one(START, techno_seq()), load=1)
    s.press("HOME", 300).press("HOME", 300).press("HOME", 700).press("PLAY", 0)
    c = Clock(s)
    s.caption("Everything goes anywhere",
              "HOME, three times: a pin matrix. 15 sources down the side, 10 destinations across. Each dot a level, "
              "0 to 127.")
    c.hold()
    s.caption("Snare and clap into the reverb",
              "KNOB" + NB + "1 the row, KNOB" + NB + "2 the column, KNOB" + NB + "3 the level. KNOB" + NB +
              "4: all or nothing.")
    s.pin("SNARE", "REV", 96).pin("CLAP", "REV", 64)
    c.hold()
    s.caption("Hats into the delay", "Only the hats. The kick stays dry.")
    s.pin("HAT", "DLY", 64)
    c.hold()
    s.caption("The lead skips the crusher",
              "FILT" + NB + "B (part B's filter) out of CRUSH, straight into the delay, the reverb and OUT. No chain "
              "you can't rewire.")
    s.pin("FILT B", "CRS", 0).pin("FILT B", "DLY", 127).pin("FILT B", "REV", 127).pin("FILT B", "OUT", 127)
    c.hold(extra=1)
    s.press("PLAY", 0)
    S.append(s)

    # 3 ---- COMP: the kick pumps the bass and the hats
    s = Scene("03-comp", "3 · COMP", after=S[-1], tail=1, slots=one(S[-1].patch, techno_seq()), load=1)
    s.press("PLAY", 0)
    c = Clock(s)
    s.caption("A compressor. Pinned, not inserted.",
              "Kick, hats and bass (FILT" + NB + "A) into the CMP column, out of OUT. One bus, squashed together.")
    s.pin("KICK", "CMP", 127).pin("KICK", "OUT", 0).pin("HAT", "CMP", 127).pin("HAT", "OUT", 0)
    s.pin("FILT A", "CRS", 0).pin("FILT A", "CMP", 127)
    c.hold()
    s.caption("20:1, fast release: it pumps",
              "FX, six times: COMP. THRESH down. Every kick shoves the bass and the hats aside. The bar is the gain "
              "it takes.")
    s.go("COMP").turn(1, 60, 2600)
    c.hold()
    s.caption("Then GAIN, to win it back", "Make-up gain, up to 24 dB: squashed and loud.")
    s.turn(4, 15, 1800)
    c.hold(extra=1)
    s.press("PLAY", 0)
    S.append(s)

    # 4 ---- SYNC: the lead tears
    s = Scene("04-sync", "4 · Hard sync", after=S[-1], tail=1, slots=one(S[-1].patch, techno_seq()), load=1)
    s.go("OSC2").press("PLAY", 0)
    c = Clock(s)
    s.caption("The lead, in SYNC",
              "OSC" + NB + "2 › ENV1 › MODE: SYNC. The modulator turns into a silent master; the carrier is dragged "
              "along behind it. A and D to a second each: a slow loop.")
    s.row("ENV1").turn(3, SYNC, 900).turn(1, 95, 1200).turn(2, 95, 1200)
    c.hold()
    s.caption("Sweep the rate",
              "MOD1 › SYNC1 up, and its AMT: the envelope drags the rate up on every note. It tears.")
    s.row("MOD1").turn(3, 120, 2400).turn(4, 96, 1600)
    c.hold(extra=1)
    s.press("PLAY", 0)
    S.append(s)

    # 5 ---- drums: pan, ghosts, rolls
    s = Scene("05-drums", "5 · Drums", after=S[-1], tail=1, slots=one(S[-1].patch, techno_seq(cur=1)), load=1)
    s.go("DRUM").press("PLAY", 0)
    c = Clock(s)
    s.caption("Every drum has its own place",
              "New: PAN on each drum's row. Hats left, clap right, the snare off-centre.")
    s.row("HAT").turn(4, 26, 1500).row("CLAP").turn(4, 100, 1500).row("SNARE").turn(4, 84, 900)
    c.hold()
    s.caption("Ghosts and rolls",
              "Snare ghosts under the groove, rolls of 2 to 8 hits into the bar line, the hat's and the snare's.")
    c.hold(extra=1)
    s.press("PLAY", 0)
    S.append(s)

    # 6 ---- stems over USB (heard on the computer, not here)
    s = Scene("06-stems", "6 · Six channels over USB", after=S[-1], tail=1, slots=one(S[-1].patch, techno_seq(cur=1)),
              load=1)
    s.press("HOME", 300).press("HOME", 300).press("HOME", 700).press("PLAY", 0)
    c = Clock(s)
    s.caption("Stems, live, one cable",
              "U2 and U3 are USB channels 3-4 and 5-6. The drums into U2, both synths into U3. Your DAW records "
              "them apart, the full mix on 1-2.")
    for d in ("KICK", "SNARE", "HAT", "CLAP"):
        s.pin(d, "U2", 127, 500)
    s.pin("FILT A", "U3", 127, 500).pin("FILT B", "U3", 127, 500)
    c.hold()
    s.caption("(You can't hear this part)", "Nothing changes in the speaker. It's for the computer.")
    c.hold()
    s.press("PLAY", 0)
    S.append(s)

    # 7 ---- chaos: push every feedback path past where it's safe
    s = Scene("07-chaos", "7 · Push it", after=S[-1], tail=2, slots=one(S[-1].patch, techno_seq(cur=1)), load=1)
    s.go("DLY").press("PLAY", 0)
    c = Clock(s)
    s.caption("Feedback past unity",
              "DELAY › FDBK to the top: the repeats feed themselves and never die. Then SYNC off, and drag the TIME.")
    s.turn(3, 70, 800).turn(2, 127, 2000)
    s.row("D.SYNC").turn(1, 0, 500).row("DELAY").turn(1, 20, 1800).turn(1, 90, 1800)
    c.hold()
    s.caption("Crush it. Drive it.",
              "CRUSH: fewer bits, a lower rate. The filter's DRIVE and RES to the top.")
    s.go("CRUSH").turn(1, 96, 1500).turn(2, 60, 1500)
    s.press("SELECT", 700).row("FILTER").turn(2, 118, 1200).row("TYPE").turn(2, 127, 900)
    c.hold()
    s.caption("It screams. It doesn't clip.",
              "The safety limiter holds the ceiling, however far you push. Everything else is your problem.")
    s.go("SCOPE")
    c.hold(extra=2)
    s.caption("Prysm is not for beginners.",
              "Every pin, every feedback path, every ratio is yours to wreck.")
    c.hold()
    s.caption("Out on a high-pass sweep",
              "FILTER › TYPE: HP" + NB + "RES up, then HPF swept up, the bass's (part A) then, SELECT, the lead's (B). "
              "Only the drums left standing.")
    s.go("FILTER").row("TYPE").turn(4, 96, 700)
    s.turn(3, 127, round(8 * c.step))
    s.press("SELECT", 400).turn(4, 96, 700)
    s.turn(3, 127, round(8 * c.step))
    c.hold(extra=1)
    s.press("PLAY", 0).wait(2500)
    S.append(s)

    # 8 ---- and then, quietly
    s = Scene("08-sweet", "8 · And then", tail=6, slots=one(SWEET, sweet_seq()), load=1)
    s.go("SCOPE").press("PLAY", 0)
    c = Clock(s, SWEET_BPM)
    s.wait(1500)
    s.caption("...but it can be gentle, too.",
              "CHORUS › MODE: I+II, after the Juno-60. A pad, a bell, a long reverb.")
    c.hold(extra=1)
    s.caption("Prysm 1.7",
              "Free, open-source firmware for the M-VAVE FM-1. Install from your browser: nicholaspratt.github.io/Prysm")
    c.hold(extra=2)
    s.press("PLAY", 0)
    S.append(s)
    return S


if __name__ == "__main__" and "--knobs" in sys.argv:
    for key, v in sorted(F.info()["knobs"].items(), key=lambda x: x[1][0]):
        print(key, v)
    sys.exit(0)

if __name__ == "__main__":
    film(scenes(), "whatsnew17", "PRYSM 1.7  ·  free firmware for the M-VAVE FM-1", only=sys.argv[1:], ver="1.7")
