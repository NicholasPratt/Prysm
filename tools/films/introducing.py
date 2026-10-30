#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Introducing Prysm: four tracks of 1:30 each, made on the FM-1, and how each sound is made, with a voice-over.

  1 Drone          VCA OPEN, VOCAL wavetables, a comb filter singing pink noise, looping mod envelopes, shimmer
  2 House          126 BPM: a split patch (an FM bass DIRECT under an FM piano), the TR kit, a cutoff build of locks
  3 Drum and bass  174 BPM: a reese (three detuned saws in MIX mode), LFO 2 in time on the cutoff, a breakbeat
  4 Berlin School  120 BPM: a sequenced bass line through a resonant LP 24 against a latched arp of FM bells

Made for phone speakers too: the bass lines sit in the second and third octaves, rich in harmonics (FM, saws), the
kicks tuned up and short, the chords and arps in the middle of the keys and above.

The narration is spoken by macOS `say` (PRYSM_VOICE, default Daniel; PRYSM_RATE, words a minute), one clip a caption,
each caption held until its clip has been spoken. The film itself is music and captions only; the voice-over is mixed
in after, the music ducked under it:

    tools/films/introducing.py            -> takes/introducing/introducing.mp4 (music only), introducing-vo.mp4
                                             (with the voice), narration.txt (the script, timed), vo.wav
    tools/films/introducing.py 03-dnb     film only that scene again (the others as filmed before), then join
    tools/films/introducing.py --vo       the voice-over again only (say another voice, or your own recordings:
                                             takes/introducing/vo/NN.wav replaces clip NN)"""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture
from film import ROOT, Scene, Seq, ffmpeg_exe, film, info, reading_ms

NAME = "introducing"
OUT = ROOT / "takes" / NAME
VOICE = os.environ.get("PRYSM_VOICE", "Daniel")
RATE = int(os.environ.get("PRYSM_RATE", "172"))
TRACK_S = 90.0                                  # each track, from its first frame to its last
NB = " "
KICK, SNARE, HAT, CLAP = range(4)
HIT, ACC, FLAM = 1, 2, 3


def w(i):                                       # wave i (0 .. 7) of a bank: its place on the WAVE knob
    return round(i * 127 / 7)


def sound(knobs):
    """A patch: the default with knobs set, {(page, row, knob 1 .. 4[, "alt"]): value} (alt: PIT1 / PIT2, the RATIO
    row's knobs in MIX or RING)."""
    I = info()
    p = bytearray(I["init"])
    for key, v in knobs.items():
        page, row, k = key[:3]
        p[I["knobs"][(page, row, k - 1) + tuple(key[3:])][0]] = v
    return bytes(p)


# ---- the voice-over: a clip a caption, spoken by `say`

NARR = []                                       # every caption's narration, in order (the .srt's)


def vo_clip(text):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "vo").mkdir(exist_ok=True)
    f = OUT / "vo" / (hashlib.sha1(f"{VOICE}|{RATE}|{text}".encode()).hexdigest()[:16] + ".aiff")
    if not f.exists():
        subprocess.run(["say", "-v", VOICE, "-r", str(RATE), "-o", str(f), text], check=True)
    return f


def vo_ms(text):
    out = subprocess.run(["afinfo", str(vo_clip(text))], capture_output=True, text=True, check=True).stdout
    return round(float(out.split("estimated duration: ")[1].split()[0]) * 1000)


def say(s, title, body, text, at=0):
    """A caption, from `at` (ms into the scene) or later, once the last has been read and spoken; spoken over."""
    s.t = max(s.t, getattr(s, "vo_end", 0), at)
    s.caption(title, body)
    NARR.append(text)
    s.vo_end = s.caps[-1][0] + 250 + vo_ms(text) + 600
    return s


def spoken(s):                                  # on to when the caption has been read and spoken
    t0, a, b, _ = s.caps[-1]
    s.t = max(s.t, t0 + reading_ms(a, b), s.vo_end)
    return s


def finish(s, stop=None):
    """The scene's end: PLAY off at `stop` (ms), and a tail making the track TRACK_S long."""
    spoken(s)
    if stop is not None:
        s.t = max(s.t, stop)
        s.press("PLAY", 0)
    lead = (capture.LEAD_MS + (capture.SPLASH_MS if s.splash else 0)) / 1000
    last = max([t for t, _ in s.ev] + [s.t]) / 1000
    s.tail = round(TRACK_S - lead - last, 3)
    if s.tail < 2:
        sys.exit(f"{s.name}: {lead + last + 2:.1f} s, over {TRACK_S} s (shorten the narration)")
    return s


def song_at(entries, steps, step_ms, t0=0):     # when each song entry starts: [(ms, pattern)] after PLAY at t0
    out, t = [], t0
    for pat, rep in entries:
        for _ in range(rep):
            out.append((round(t), pat))
            t += steps[pat] * step_ms
    return out, round(t)


def song(q, entries, lens):
    q.raw("seqs.mode = 1;", f"seqs.song_len = {len(entries)};")
    q.raw(*[f"seqs.song_pat[{i}] = {p}; seqs.song_rep[{i}] = {r};" for i, (p, r) in enumerate(entries)])
    q.raw(*[f"seqs.pat[{p}].len = {n};" for p, n in lens.items()])
    return q


def held(q, pat, step, note, steps, lane=0, accent=False, slide=False):   # a note held over steps, sliding from its end
    q.note(pat, step, note, lane=lane, steps=steps, accent=accent)
    if slide: q.raw(f"seqs.pat[{pat}].slide[{step + steps - 2}] = 1;")
    return q


def kit(q, n, lvl, tune, dec, drive=0, crush=0, rev=0, dly=0):
    q.raw(f"seqs.dr_kit = {n};", f"seqs.dr_drive = {drive};", f"seqs.dr_crush = {crush};", f"seqs.dr_rev = {rev};",
          f"seqs.dr_dly = {dly};")
    q.raw(*[f"seqs.dr_lvl[{d}] = {v};" for d, v in enumerate(lvl)])
    q.raw(*[f"seqs.dr_tune[{d}] = {v};" for d, v in enumerate(tune)])
    q.raw(*[f"seqs.dr_dec[{d}] = {v};" for d, v in enumerate(dec)])
    return q


def roll(q, pat, step, hits):                   # the hat's step played 2 .. 8 times
    q.drum(pat, HAT, [step]).raw(f"ratchet_put(&seqs.pat[{pat}], {step - 1}, {[1, 2, 3, 4, 6, 8].index(hits)});")
    return q


def lock(q, pat, lane, dest, values):           # a lock lane: {step 1 .. 64: value}
    q.raw(f"seqs.pat[{pat}].lk_dest[{lane}] = {dest};")
    q.raw(*[f"seqs.pat[{pat}].lk[{lane}][{k - 1}] = {v + 1};" for k, v in values.items()])
    return q


# ---- 1: the drone

DRONE = sound({
    # OSC 1: a VOCAL carrier (ah), FM from a sine x2, and a HARM wave x3 into that, both slow to come and go
    ("OSC1", "WAVE", 1): 3, ("OSC1", "WAVE", 2): w(2), ("OSC1", "WAVE2", 1): 1, ("OSC1", "WAVE2", 2): w(3),
    ("OSC1", "RATIO", 2): 3, ("OSC1", "RATIO", 3): 4,
    ("OSC1", "ENV", 1): 95, ("OSC1", "ENV", 2): 110, ("OSC1", "ENV", 3): 105, ("OSC1", "ENV", 4): 110,
    ("OSC1", "MIX", 1): 34, ("OSC1", "MIX", 2): 26, ("OSC1", "MIX", 3): 120,
    # OSC 2: an octave up, a HARM carrier with a VOCAL layer (MIX) another octave up, a few cents sharp
    ("OSC2", "WAVE", 1): 1, ("OSC2", "WAVE", 2): w(4), ("OSC2", "WAVE", 3): 3, ("OSC2", "WAVE", 4): w(5),
    ("OSC2", "MODE", 1): 2, ("OSC2", "RATIO", 2, "alt"): 36, ("OSC2", "MODE", 3): 79,
    ("OSC2", "ENV", 2): 110, ("OSC2", "MIX", 1): 72, ("OSC2", "MIX", 3): 100,
    ("MIX", "OSC", 1): 110, ("MIX", "OSC", 2): 82, ("MIX", "OSC", 3): 36, ("MIX", "ROUTE", 2): 1,   # OSC 2 DIRECT
    # the noise: pink, through the comb (turned up on screen)
    ("NOISE", "NOISE", 1): 1, ("NOISE", "NOISE", 2): 95, ("NOISE", "NOISE", 3): 0,
    # the filter: COMB+, its peaks on each note's harmonics (KEY 100 %, CUT 64), no envelope on it
    ("FILTER", "FILTER", 1): 64, ("FILTER", "FILTER", 2): 104, ("FILTER", "FILTER", 3): 64, ("FILTER", "FILTER", 4): 127,
    ("FILTER", "TYPE", 1): 2, ("FILTER", "TYPE", 3): 20, ("FILTER", "COMB", 1): 55, ("FILTER", "COMB", 2): 0,
    ("AMP", "ENV", 1): 100, ("AMP", "ENV", 2): 100, ("AMP", "ENV", 3): 127, ("AMP", "ENV", 4): 112,
    # the LFO: a slow triangle, free, on the vowel (WAVE1) and a little FM
    ("LFO", "LFO", 1): 1, ("LFO", "LFO", 2): 22, ("LFO", "LFO", 4): 0,
    ("LFO", "DEST", 1): 3, ("LFO", "DEST", 2): 92, ("LFO", "DEST", 3): 7, ("LFO", "DEST", 4): 76,
    # M.ENV 1 looping (2 s up, 5 s down) on OSC 2's modulators' waves; M.ENV 2 looping on mod 1's pitch, a little
    ("MENV", "ENV", 1): 105, ("MENV", "ENV", 2): 116, ("MENV", "ENV", 3): 0, ("MENV", "ENV", 4): 110,
    ("MENV", "DEST", 1): 1, ("MENV", "DEST", 2): 6, ("MENV", "DEST", 3): 100,
    ("MENV2", "ENV", 1): 108, ("MENV2", "ENV", 2): 118, ("MENV2", "ENV", 3): 0, ("MENV2", "ENV", 4): 110,
    ("MENV2", "DEST", 1): 1, ("MENV2", "DEST", 2): 16, ("MENV2", "DEST", 3): 67,
    # LFO 2: a slow sine on the comb's spread
    ("LFO2", "LFO", 1): 0, ("LFO2", "LFO", 2): 14, ("LFO2", "DEST", 1): 15, ("LFO2", "DEST", 2): 80,
    # the matrix: RANDOM onto the pitch (a drift), KEY onto MOD1 (high notes cleaner)
    ("MATRIX", "MOD 1", 1): 8, ("MATRIX", "MOD 1", 2): 1, ("MATRIX", "MOD 1", 3): 66,
    ("MATRIX", "MOD 2", 1): 6, ("MATRIX", "MOD 2", 2): 7, ("MATRIX", "MOD 2", 3): 50,
    ("MFX", "MOD FX", 1): 1, ("MFX", "MOD FX", 2): 6, ("MFX", "MOD FX", 3): 90, ("MFX", "MOD FX", 4): 50,
    ("MFX", "TONE", 1): 50, ("MFX", "TONE", 2): 84,
    ("CHO", "CHORUS", 1): 25, ("CHO", "CHORUS", 2): 80, ("CHO", "CHORUS", 3): 60,
    ("DLY", "DELAY", 1): 103, ("DLY", "DELAY", 2): 60, ("DLY", "DELAY", 3): 40, ("DLY", "DELAY", 4): 1,   # 600 ms
    ("DLY", "D.MOD", 1): 30, ("DLY", "D.MOD", 2): 25, ("DLY", "D.MOD", 3): 19, ("DLY", "D.MOD", 4): 50,  # up a 5th
    ("REV", "REVERB", 1): 118, ("REV", "REVERB", 2): 60, ("REV", "REVERB", 3): 78, ("REV", "REVERB", 4): 30,
    ("REV", "R.MOD", 1): 30, ("REV", "R.MOD", 2): 80, ("REV", "R.MOD", 3): 30,
})
DM9, BBMAJ7, G11, DMADD9 = (62, 69, 72, 76), (58, 65, 69, 74), (55, 62, 69, 72), (62, 69, 76, 77)


# ---- 2: house

H_BPM, H_STEP = 126, 60000 / 126 / 4
HOUSE = sound({
    # OSC 1, the bass: a sine FM'd by a sine x1 (and x2 into it), short: a plucked, round FM bass
    ("OSC1", "RATIO", 2): 1, ("OSC1", "RATIO", 3): 3,
    ("OSC1", "ENV", 1): 0, ("OSC1", "ENV", 2): 62, ("OSC1", "ENV", 3): 0, ("OSC1", "ENV", 4): 52,
    ("OSC1", "MIX", 1): 82, ("OSC1", "MIX", 2): 40, ("OSC1", "MIX", 3): 127,
    # OSC 2, the piano: a sine, a sine x1 on it, a sine x7 into that (the hammer's edge)
    ("OSC2", "WAVE", 2): 0, ("OSC2", "RATIO", 2): 1, ("OSC2", "RATIO", 3): 8,
    ("OSC2", "ENV", 1): 0, ("OSC2", "ENV", 2): 70, ("OSC2", "ENV", 3): 0, ("OSC2", "ENV", 4): 45,
    ("OSC2", "MIX", 1): 56, ("OSC2", "MIX", 2): 34, ("OSC2", "MIX", 3): 112,
    ("MIX", "OSC", 1): 105, ("MIX", "OSC", 2): 110, ("MIX", "OSC", 4): 64, ("MIX", "ROUTE", 1): 1,   # bass DIRECT
    ("MIX", "SPLIT", 1): 58,                    # A#3: the bass below, the piano from it up
    ("FILTER", "FILTER", 1): 96, ("FILTER", "FILTER", 2): 30, ("FILTER", "FILTER", 3): 74,
    ("FILTER", "TYPE", 2): 20, ("FILTER", "TYPE", 3): 30,
    ("FENV", "ENV", 1): 0, ("FENV", "ENV", 2): 70, ("FENV", "ENV", 3): 40, ("FENV", "ENV", 4): 60,
    ("AMP", "ENV", 1): 0, ("AMP", "ENV", 2): 80, ("AMP", "ENV", 3): 56, ("AMP", "ENV", 4): 64,
    # M.ENV 1: a quick decay onto MOD1: every note starts bright and settles
    ("MENV", "ENV", 1): 0, ("MENV", "ENV", 2): 72, ("MENV", "ENV", 3): 0, ("MENV", "ENV", 4): 60,
    ("MENV", "DEST", 2): 7, ("MENV", "DEST", 3): 84,
    ("MATRIX", "MOD 1", 1): 8, ("MATRIX", "MOD 1", 2): 10, ("MATRIX", "MOD 1", 3): 72,   # RANDOM onto CUTOFF
    ("ARP", "TIME", 1): H_BPM - 40, ("ARP", "TIME", 3): 10,
    ("CHO", "CHORUS", 1): 40, ("CHO", "CHORUS", 2): 50, ("CHO", "CHORUS", 3): 30,
    ("DLY", "DELAY", 1): 61, ("DLY", "DELAY", 2): 45, ("DLY", "DELAY", 3): 40, ("DLY", "D.MOD", 4): 60,   # 3/16
    ("REV", "REVERB", 1): 75, ("REV", "REVERB", 2): 60, ("REV", "REVERB", 3): 30,
})
H_PROG = [(45, (64, 67, 72)), (41, (64, 69, 72)), (50, (65, 69, 72)), (43, (62, 67, 71))]   # Am7 Fmaj7 Dm7 G
H_SONG = [(0, 2), (1, 3), (2, 1), (3, 1), (1, 4)]   # intro, groove, breakdown, build, the drop


def house_seq():
    q = Seq()
    for p in range(4):
        q.raw(f"seqs.pat[{p}].lk_dest[1] = LK_DSEND;")   # (no send but the throws)
    for bar, (root, ch) in enumerate(H_PROG):
        b = 16 * bar
        for p in (0, 1, 3):                     # the beat
            q.drum(p, KICK, [b + 1, b + 5, b + 9, b + 13])
            if p != 3: q.drum(p, CLAP, [b + 5, b + 13])
            q.drum(p, HAT, [b + k for k in (2, 4, 6, 8, 10, 12, 14, 16)]).drum(p, HAT, [b + 3, b + 7, b + 11, b + 15], ACC)
        for p in (0, 1):                        # the bass: off the beat, the fifth between
            for k, iv, st, acc in ((3, 0, 2, 0), (7, 0, 1, 0), (8, 7, 1, 0), (11, 0, 2, 1), (14, 0, 1, 0), (16, 7, 1, 0)):
                q.note(p, b + k, root + iv, steps=st, accent=bool(acc))
        for p in (1, 3):                        # the piano: 3 3 3 3 2 2
            for k in (1, 4, 7, 10, 13, 15):
                for lane, n in enumerate(ch):
                    q.note(p, b + k, n, lane=1 + lane, steps=2)
        for lane, n in enumerate(ch):           # the breakdown: each chord held, the hats open
            q.note(2, b + 1, n, lane=1 + lane, steps=14)
        q.drum(2, HAT, [b + 3, b + 7, b + 11, b + 15], ACC)
    q.drum(2, CLAP, [61, 63, 64])
    q.drum(1, CLAP, [64])
    stabs = [16 * bar + k for bar in range(4) for k in (1, 4, 7, 10, 13, 15)]
    lock(q, 1, 0, "LK_CUTOFF", dict(zip(stabs, [84, 96, 88, 104, 92, 110] * 4)))   # each stab its brightness
    lock(q, 1, 1, "LK_DSEND", {63: 110})        # a throw: the last stab into the delay
    q.drum(3, SNARE, list(range(1, 33, 4)) + list(range(33, 49, 2)) + list(range(49, 65)))   # the build: 4s, 8ths, 16ths
    q.drum(3, SNARE, [61, 62, 63, 64], ACC)
    roll(q, 3, 64, 4)
    lock(q, 3, 0, "LK_CUTOFF", {k: 46 + (k - 1) * 66 // 63 for k in range(1, 65)})   # the build: dark to bright
    song(q, H_SONG, {p: 64 for p in range(4)})
    kit(q, 1, lvl=(116, 88, 70, 100), tune=(29, 28, 24, 25), dec=(100, 110, 127, 127), drive=40, rev=24)
    return q


# ---- 3: drum and bass

D_BPM, D_STEP = 174, 60000 / 174 / 4
DNB = sound({
    # OSC 1, the reese: three saws, both modulators in MIX at the carrier's pitch, one sharp, one flat
    ("OSC1", "WAVE", 2): w(4), ("OSC1", "WAVE", 3): 0, ("OSC1", "WAVE", 4): w(4),
    ("OSC1", "WAVE2", 1): 0, ("OSC1", "WAVE2", 2): w(4),
    ("OSC1", "MODE", 1): 2, ("OSC1", "MODE", 2): 2, ("OSC1", "MODE", 3): 84, ("OSC1", "MODE", 4): 44,
    ("OSC1", "ENV", 2): 70, ("OSC1", "ENV", 4): 70,
    ("OSC1", "MIX", 1): 100, ("OSC1", "MIX", 2): 92, ("OSC1", "MIX", 3): 120,
    # OSC 2, the pads: vowels (ae), a layer of ih an octave up, a little sharp; DIRECT, past the filter
    ("OSC2", "WAVE", 1): 3, ("OSC2", "WAVE", 2): w(3), ("OSC2", "WAVE", 3): 3, ("OSC2", "WAVE", 4): w(6),
    ("OSC2", "MODE", 1): 2, ("OSC2", "RATIO", 2, "alt"): 36, ("OSC2", "MODE", 3): 74,
    ("OSC2", "ENV", 2): 90, ("OSC2", "MIX", 1): 55, ("OSC2", "MIX", 3): 95,
    ("MIX", "OSC", 1): 86, ("MIX", "OSC", 2): 112, ("MIX", "OSC", 4): 64, ("MIX", "ROUTE", 2): 1,
    ("MIX", "SPLIT", 1): 60,                    # C4: the reese below, the pads from it up
    ("MIX", "GLIDE", 1): 62, ("MIX", "GLIDE", 2): 1,
    ("FILTER", "FILTER", 1): 60, ("FILTER", "FILTER", 2): 52, ("FILTER", "FILTER", 3): 74, ("FILTER", "FILTER", 4): 40,
    ("FILTER", "TYPE", 1): 1, ("FILTER", "TYPE", 2): 62, ("FILTER", "TYPE", 3): 50,
    ("FENV", "ENV", 1): 0, ("FENV", "ENV", 2): 85, ("FENV", "ENV", 3): 60, ("FENV", "ENV", 4): 70,
    ("AMP", "ENV", 1): 50, ("AMP", "ENV", 2): 90, ("AMP", "ENV", 3): 115, ("AMP", "ENV", 4): 80,
    # the LFO: mod 1's pitch, a little (the detune moving), and the pads' vowel
    ("LFO", "LFO", 2): 35, ("LFO", "DEST", 1): 16, ("LFO", "DEST", 2): 66, ("LFO", "DEST", 3): 4,
    ("LFO", "DEST", 4): 80,
    # LFO 2, in time: a triangle every half note on the cutoff
    ("LFO2", "LFO", 1): 1, ("LFO2", "LFO", 3): 4, ("LFO2", "DEST", 1): 10, ("LFO2", "DEST", 2): 82,
    ("ARP", "TIME", 1): D_BPM - 40,
    ("CHO", "CHORUS", 1): 30, ("CHO", "CHORUS", 2): 70, ("CHO", "CHORUS", 3): 60,
    ("DLY", "DELAY", 1): 44, ("DLY", "DELAY", 2): 45, ("DLY", "DELAY", 3): 25, ("DLY", "D.MOD", 4): 55,   # 3/16
    ("REV", "REVERB", 1): 95, ("REV", "REVERB", 2): 55, ("REV", "REVERB", 3): 38, ("REV", "R.MOD", 3): 20,
})
D_PROG = [(52, (66, 71, 74)), (48, (67, 71, 74)), (45, (67, 71, 72)), (47, (66, 69, 74))]   # Em9 Cmaj9 Am9 Bm7
D_SONG = [(0, 2), (1, 4), (2, 2), (3, 1), (1, 4), (2, 2)]   # intro, beat, variation, break, beat, variation


def dnb_seq():
    q = Seq()
    for bar, (root, ch) in enumerate(D_PROG):
        b = 16 * bar
        for p in range(4):                      # the pads: a chord a bar
            for lane, n in enumerate(ch):
                q.note(p, b + 1, n, lane=1 + lane, steps=16)
        for p in (1, 2, 3):                     # the reese: the root, a slide to the fifth, back
            held(q, p, b + 1, root, 10, slide=True)
            q.note(p, b + 11, root + 7, steps=2)
            q.note(p, b + 13, root, steps=4, accent=True)
        for p in (1, 2):                        # the beat: two-step, ghost snares, the hats on the 8ths
            q.drum(p, KICK, [b + 1, b + 11]).drum(p, SNARE, [b + 5, b + 13], ACC).drum(p, SNARE, [b + 8, b + 15])
            q.drum(p, HAT, [b + k for k in range(1, 17, 2)])
        q.drum(0, HAT, [b + k for k in range(1, 17, 2)])
    q.drum(1, HAT, [32, 63], ACC)
    roll(q, 1, 48, 3)
    q.drum(1, SNARE, [61], FLAM)
    q.drum(2, KICK, [7, 23, 39, 55]).drum(2, CLAP, [13, 29, 45, 61])   # the variation: an extra kick, a clap
    for k in (16, 32, 46, 48, 62, 64): roll(q, 2, k, 3)
    lock(q, 2, 0, "LK_CUTOFF", {11: 80, 27: 84, 43: 80, 59: 90})    # the slide's fifth, open
    q.drum(0, SNARE, [53, 57, 61, 62, 63, 64]).drum(3, SNARE, list(range(49, 65)))   # fills into the beat
    q.drum(3, SNARE, [61, 62, 63, 64], ACC)
    song(q, D_SONG, {p: 64 for p in range(4)})
    kit(q, 1, lvl=(118, 104, 74, 72), tune=(28, 27, 26, 26), dec=(92, 110, 110, 127), drive=55, crush=22, rev=20)
    return q


# ---- 4: Berlin School

B_BPM, B_STEP = 120, 60000 / 120 / 4
BERLIN = sound({
    # OSC 1, the sequence: a saw, a little FM from a sine x1
    ("OSC1", "WAVE", 2): w(4), ("OSC1", "MIX", 1): 18, ("OSC1", "ENV", 2): 70,
    # OSC 2, the arp's bells: a sine, a sine x3 on it, a sine x1 into that; DIRECT
    ("OSC2", "WAVE", 2): 0, ("OSC2", "RATIO", 2): 4, ("OSC2", "RATIO", 3): 1,
    ("OSC2", "ENV", 2): 75, ("OSC2", "ENV", 4): 60, ("OSC2", "MIX", 1): 52, ("OSC2", "MIX", 2): 20,
    ("OSC2", "MIX", 3): 92,
    ("MIX", "OSC", 1): 104, ("MIX", "OSC", 2): 120, ("MIX", "OSC", 4): 64, ("MIX", "ROUTE", 2): 1,
    ("MIX", "SPLIT", 1): 72, ("MIX", "SPLIT", 3): 1,   # C5: the arp above, an octave down
    # the LP 24: low, resonant, a short envelope: the pluck
    ("FILTER", "FILTER", 1): 52, ("FILTER", "FILTER", 2): 98, ("FILTER", "FILTER", 3): 94,
    ("FILTER", "TYPE", 1): 1, ("FILTER", "TYPE", 2): 30, ("FILTER", "TYPE", 3): 24,
    ("FENV", "ENV", 1): 0, ("FENV", "ENV", 2): 66, ("FENV", "ENV", 3): 0, ("FENV", "ENV", 4): 60,
    ("AMP", "ENV", 1): 0, ("AMP", "ENV", 2): 80, ("AMP", "ENV", 3): 70, ("AMP", "ENV", 4): 62,
    ("LFO", "LFO", 1): 1, ("LFO", "LFO", 2): 18, ("LFO", "LFO", 4): 0, ("LFO", "DEST", 1): 3, ("LFO", "DEST", 2): 74,
    # LFO 2: four bars a cycle, on the cutoff
    ("LFO2", "LFO", 1): 1, ("LFO2", "LFO", 3): 1, ("LFO2", "DEST", 1): 10, ("LFO2", "DEST", 2): 86,
    ("MATRIX", "MOD 1", 1): 6, ("MATRIX", "MOD 1", 2): 10, ("MATRIX", "MOD 1", 3): 80,   # KEY onto CUTOFF
    ("MATRIX", "MOD 2", 1): 8, ("MATRIX", "MOD 2", 2): 7, ("MATRIX", "MOD 2", 3): 74,    # RANDOM onto MOD1
    # the arp: on, latched, UP over 2 octaves, 1/16
    ("ARP", "ARP", 1): 1, ("ARP", "ARP", 2): 0, ("ARP", "ARP", 3): 3, ("ARP", "ARP", 4): 1,
    ("ARP", "TIME", 1): B_BPM - 40, ("ARP", "TIME", 2): 40, ("ARP", "TIME", 4): 1,
    ("MFX", "MOD FX", 1): 1, ("MFX", "MOD FX", 2): 12, ("MFX", "MOD FX", 3): 85, ("MFX", "MOD FX", 4): 55,
    ("MFX", "TONE", 1): 55, ("MFX", "TONE", 2): 88,
    ("CHO", "CHORUS", 1): 20, ("CHO", "CHORUS", 2): 60, ("CHO", "CHORUS", 3): 35,
    ("DLY", "DELAY", 1): 64, ("DLY", "DELAY", 2): 58, ("DLY", "DELAY", 3): 38, ("DLY", "D.MOD", 1): 35,  # 3/16
    ("DLY", "D.MOD", 2): 22, ("DLY", "D.MOD", 4): 55,
    ("REV", "REVERB", 1): 108, ("REV", "REVERB", 2): 60, ("REV", "REVERB", 3): 42, ("REV", "R.MOD", 3): 22,
})
MINOR = [0, 12, 7, 0, 10, 0, 7, 3, 0, 12, 7, 0, 15, 14, 10, 7]
MINOR2 = [0, 12, 7, 0, 10, 0, 7, 3, 0, 12, 7, 12, 15, 12, 10, 3]
MAJOR = [0, 12, 7, 0, 11, 0, 7, 4, 0, 12, 7, 0, 16, 14, 11, 7]
MIXO = [0, 12, 7, 0, 10, 0, 7, 4, 0, 12, 7, 0, 16, 14, 10, 7]
HIGH = [0, 12, 7, 12, 15, 12, 7, 3, 0, 12, 19, 12, 15, 14, 10, 7]
B_SONG = [(0, 3), (1, 2), (0, 2), (2, 2), (0, 4), (1, 2), (2, 2), (3, 4)]
B_ARP = {0: (76, 79, 83), 1: (76, 79, 84), 2: (78, 81, 86), 3: (76, 79, 83, 86)}   # Em, C, D, Em7 (an octave down)


def berlin_seq():
    q = Seq()
    for p, (root, a, b) in enumerate(((52, MINOR, MINOR2), (48, MAJOR, MAJOR), (50, MIXO, MIXO), (52, MINOR, HIGH))):
        for k, iv in enumerate(a + b):
            q.note(p, k + 1, root + iv, accent=k % 8 == 0)
    song(q, B_SONG, {p: 32 for p in range(4)})
    return q


SLOTS = {1: (DRONE, Seq()), 2: (HOUSE, house_seq()), 3: (DNB, dnb_seq()), 4: (BERLIN, berlin_seq())}


# ---- the film

def scenes():
    S = []
    NARR.clear()

    # 1 ---- the drone
    s = Scene("01-drone", "1 · Drone", splash=True, slots=SLOTS, load=1)
    s.go("AMP").row("VCA")
    s.wait(600).on(*DM9).wait(0)
    say(s, "Introducing Prysm", "A free synth firmware for the M-VAVE FM-1",
        "This is Prysm, a free firmware that turns the M-VAVE FM-1 into a four-voice wavetable FM synth. Four "
        "tracks made on it, and how each sound is built. First, a drone.")
    say(s, "An instant drone", "AMP › VCA › OPEN",
        "OPEN, on the AMP page, holds the amp open, like an analog VCA's bias. Let go of the chord and it keeps "
        "sounding, every knob still live.")
    s.wait(1200).turn(1, 96, 2500).wait(1000).off(*DM9)
    say(s, "Vowels", "OSC" + NB + "1: a VOCAL wave, FM from a sine x2",
        "Oscillator one is a chain of three operators: a vocal wavetable, morphing from vowel to vowel, with a sine "
        "modulator at twice its pitch.")
    s.go("OSC1").row("WAVE").turn(2, 58, 5000)
    say(s, "A layer an octave up", "OSC" + NB + "2: MIX mode, PIT1 +12, FINE1 sharp",
        "Oscillator two plays an octave up, its modulator in MIX mode: a second layer, a few cents sharp.")
    s.go("OSC2").row("MODE")
    s.play([BBMAJ7], dur=4000, at=s.t + 1000)
    say(s, "A comb filter", "COMB+, KEY 100 %, SPREAD up: bells",
        "Oscillator one goes through a comb filter, tuned to each note's harmonics. SPREAD pulls the peaks apart: "
        "the string becomes a bell.")
    s.go("FILTER").row("COMB").turn(2, 44, 4500).turn(2, 14, 3000)
    say(s, "Noise that sings", "NOISE: PINK, through the comb",
        "Pink noise goes through the same comb, so the noise sings the chord.")
    s.go("NOISE").turn(3, 50, 3500)
    say(s, "Slow movement", "LFO › WAVE1 · M.ENV 1 looping · LFO" + NB + "2 › SPREAD",
        "A slow LFO on the vowel, a looping envelope on the second oscillator, LFO two on the spread, and a random "
        "drift in the mod matrix.")
    s.go("LFO").row("DEST").wait(2500).go("MATRIX")
    s.play([G11], dur=4000, at=s.t)
    say(s, "Shimmer", "Phaser, chorus, a delay rising in fifths, shimmer reverb",
        "Then a phaser, chorus, a delay climbing in fifths, and a shimmer reverb.")
    s.go("REV").row("R.MOD").turn(3, 60, 4000)
    s.play([DMADD9], dur=5000, at=s.t + 1500)
    s.wait(9000)
    say(s, "Let it fade", "OPEN back to 0", "Turn OPEN back to zero, and it fades away.")
    s.go("AMP").row("VCA").turn(1, 0, 4000)
    S.append(finish(s))

    # 2 ---- house
    entries, end = song_at(H_SONG, {p: 64 for p in range(4)}, H_STEP)
    starts = [t for t, _ in entries]            # (each entry's start: the intro 0, 1; groove 2 .. 4; break 5; build 6; drop 7 ..)
    s = Scene("02-house", "2 · House", slots=SLOTS, load=2)
    s.go("DRUM").press("PLAY", 0)
    t0 = s.t
    say(s, "House, 126" + NB + "BPM", "One patch: a sound, its sequencer, its drums",
        "Track two: house. One patch: the sound, and the sequencer and drums saved with it.")
    say(s, "Four drums, the TR kit", "KICK, SNARE, HAT, CLAP; accents open the hat",
        "Four drums, three kits. This is TR, the kick tuned up and driven, so it clicks through on a phone.")
    s.row("KICK")
    say(s, "A split keyboard", "MIX › SPLIT: OSC" + NB + "1 below A#3, OSC" + NB + "2 above",
        "The keyboard is split: below A sharp three, oscillator one, the bass. Above, oscillator two, the piano.",
        at=t0 + starts[2] - 600)
    s.go("MIX").row("SPLIT")
    say(s, "An FM bass", "A sine, a sine x1 on it, x2 into that; DIRECT",
        "The bass is FM, a sine on a sine: harmonics a phone can play. It goes direct, past the filter.")
    s.go("OSC1").row("RATIO")
    say(s, "An FM piano", "A sine x1, a sine x7 for the hammer; M.ENV" + NB + "1 on MOD1",
        "The piano is FM too, with a modulator at seven times for the hammer, and a mod envelope that makes each "
        "note start bright.")
    s.go("OSC2").row("RATIO")
    say(s, "The breakdown", "No kick; PRESETS: the cutoff down", "The breakdown. I take the cutoff down by hand.",
        at=t0 + starts[5] + 200)
    s.go("FILTER").turn("cut", 62, 5000)
    say(s, "The build", "LANE1: CUTOFF, a lock on every step, rising",
        "In the build, every step has a cutoff lock, rising over four bars. No knob is touched.",
        at=t0 + starts[6] - 200)
    s.turn("cut", 96, 1200, block=False)
    s.go("SEQ").row("PATTERN").twist(1, 3, "PATT: P4").row("LOCK")
    say(s, "The drop", "A cutoff locked on every stab; one delay throw",
        "The drop. Each stab has its own cutoff lock, and the last of the phrase is thrown into the delay.",
        at=t0 + starts[7] + 300)
    s.row("PATTERN").twist(1, -2, "PATT: P2").row("LOCK")
    spoken(s).wait(1500).go("DRUM")
    S.append(finish(s, t0 + end))

    # 3 ---- drum and bass
    entries, end = song_at(D_SONG, {p: 64 for p in range(4)}, D_STEP)
    starts = [t for t, _ in entries]            # (intro 0, 1; beat 2 .. 5; variation 6, 7; break 8; beat 9 ..)
    s = Scene("03-dnb", "3 · Drum and bass", slots=SLOTS, load=3)
    s.go("SEQ").row("SONG").press("PLAY", 0)
    t0 = s.t
    say(s, "Drum and bass, 174" + NB + "BPM", "SEQ › SONG: four patterns, in order",
        "Track three: drum and bass. The sequencer plays a song: four patterns, in the order you set.")
    say(s, "A two-step beat", "Ghost snares, a flam, hat rolls; BUS: drive and crush",
        "A two-step beat, with ghost snares, a flam, and hat rolls: one step played three times. Drive and bit "
        "crush on the drum bus.", at=t0 + starts[2] - 300)
    s.go("DRUM").row("BUS")
    say(s, "A reese bass", "Three saws: both modulators in MIX, FINE1 sharp, FINE2 flat",
        "The bass is a reese: three saws. Both modulators are saws in MIX mode, one tuned sharp, one flat.")
    s.go("OSC1").row("MODE")
    say(s, "Filtered in time", "LP" + NB + "24 with drive; LFO" + NB + "2 at 1/2 on CUTOFF",
        "Through the 24 dB low-pass with drive, and LFO two, synced to the tempo, moves the cutoff every half "
        "note.")
    s.go("LFO2")
    say(s, "Pads past the filter", "OSC" + NB + "2: vowels with a layer; ROUTE: DIRECT",
        "Above the split, vowel pads with a layer an octave up, routed past the filter.")
    s.go("MIX").row("ROUTE")
    say(s, "The break", "The drums stop; the reese alone", "The break. The drums drop out.",
        at=t0 + starts[8] + 300)
    s.go("OSC1").row("MIX").turn(1, 76, 2000).turn(1, 100, 2000)
    s.t = t0 + starts[9] - 400
    s.go("DRUM").row("STEP")
    S.append(finish(s, t0 + end))

    # 4 ---- Berlin School
    entries, end = song_at(B_SONG, {p: 32 for p in range(4)}, B_STEP)
    s = Scene("04-berlin", "4 · Berlin School", slots=SLOTS, load=4)
    s.go("SEQ").press("PLAY", 0)
    t0 = s.t
    arp_at = [(t, p) for i, (t, p) in enumerate(entries) if i == 2 or (i > 2 and p != entries[i - 1][1])]
    for t, p in arp_at:                         # the arp's chords, from the third pattern, as the song changes key
        s.play([B_ARP[p]], dur=150, at=t0 + t)
    say(s, "Berlin School, 120" + NB + "BPM", "Two patterns running against each other",
        "Last, a track in the spirit of Tangerine Dream: two sequences of notes, running against each other.")
    say(s, "The sequence", "SEQ: 16ths, through E minor, C and D",
        "The sequencer plays a sixteenth-note bass line, taken through E minor, C and D by the song.")
    s.row("SONG")
    say(s, "A resonant saw", "LP" + NB + "24, RES high, a short envelope; LFO" + NB + "2 over 4 bars",
        "Its sound: a saw through the 24 dB low-pass, the resonance high, a short envelope for the pluck. LFO two "
        "sweeps the cutoff over four bars, and I open it further by hand.")
    s.go("FILTER").turn("cut", 68, 12000)
    say(s, "The arpeggiator against it", "ARP: UP, 2 octaves, latched; above the split",
        "Above the split, the arpeggiator: three notes, up over two octaves, latched. Six steps against sixteen, "
        "so the two patterns drift apart and meet again.")
    s.go("ARP")
    say(s, "Glass", "OSC" + NB + "2: a sine, a sine x3 on it; DIRECT",
        "Its sound is an FM bell, a sine with a modulator at three times the pitch, routed past the filter.")
    s.go("OSC2").row("RATIO")
    say(s, "Phaser and tape echo", "MOD" + NB + "FX: PHASER; DELAY with wow",
        "A slow phaser over all of it, a ping-pong delay with a little wow, like tape, and a long reverb.")
    s.go("MFX")
    say(s, "No two alike", "MATRIX: KEY › CUTOFF, RANDOM › MOD1",
        "In the mod matrix, higher notes open the filter, and a random value on every note varies the FM, so no "
        "two repeats are quite the same.")
    s.go("MATRIX")
    say(s, "Get Prysm", "Free: nicholaspratt.github.io/Prysm",
        "Prysm is free. Install it from your browser at nicholaspratt dot github dot io, slash Prysm. The manual "
        "and a browser editor are there too. Thanks for listening.")
    s.go("ARP")
    s.t = max(s.t, t0 + end - 9000)
    s.go("FILTER").turn("cut", 46, 7000)
    S.append(finish(s, t0 + end))
    return S


# ---- the voice-over, mixed in

def srt_starts(path):
    out = []
    for block in path.read_text().strip().split("\n\n"):
        h, m, r = block.splitlines()[1].split(" --> ")[0].split(":")
        out.append(int(h) * 3600 + int(m) * 60 + float(r.replace(",", ".")))
    return out


def voice_over():
    import numpy as np
    ff, fs = ffmpeg_exe(), 44100
    starts = srt_starts(OUT / f"{NAME}.srt")
    if len(starts) != len(NARR):
        sys.exit(f"{len(starts)} captions in the .srt, {len(NARR)} narrations: film again")
    vo = np.zeros(int((starts[-1] + 60) * fs), np.float32)
    lines = []
    for i, (t, text) in enumerate(zip(starts, NARR)):
        own = OUT / "vo" / f"{i + 1:02d}.wav"   # (a recording of your own replaces the clip)
        src = own if own.exists() else vo_clip(text)
        raw = subprocess.run([ff, "-loglevel", "error", "-i", str(src), "-f", "f32le", "-ac", "1", "-ar", str(fs), "-"],
                             capture_output=True, check=True).stdout
        x = np.frombuffer(raw, np.float32)
        a = int((t + 0.25) * fs)
        vo[a:a + len(x)] += x[:len(vo) - a]
        lines.append(f"{i + 1:02d}  {int(t // 60)}:{t % 60:04.1f}  ({len(x) / fs:.1f} s)  {text}")
    vo *= 0.6 / max(1e-6, float(np.abs(vo).max()))   # (-4.4 dB: the limiter keeps the sum under -1 dB)
    wav = OUT / "vo.wav"
    subprocess.run([ff, "-y", "-loglevel", "error", "-f", "f32le", "-ar", str(fs), "-ac", "1", "-i", "-",
                    str(wav)], input=vo.tobytes(), check=True)
    (OUT / "narration.txt").write_text("\n".join(lines) + "\n")
    chain = ("[1:a]highpass=f=90,acompressor=threshold=0.1:ratio=3:attack=5:release=120,pan=stereo|c0=c0|c1=c0,"
             "asplit=2[v][k];"
             "[0:a][k]sidechaincompress=threshold=0.015:ratio=6:attack=20:release=450[m];"
             "[m][v]amix=inputs=2:normalize=0:duration=first,alimiter=limit=0.89:level=false[a]")
    subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(OUT / f"{NAME}.mp4"), "-i", str(wav), "-filter_complex",
                    chain, "-map", "0:v", "-map", "[a]", "-c:v", "copy", "-c:a", "aac", "-b:a", "256k",
                    str(OUT / f"{NAME}-vo.mp4")], check=True)
    print(f"-> {OUT / (NAME + '-vo.mp4')}, narration.txt ({len(NARR)} clips, voice {VOICE})")


if __name__ == "__main__":
    args = sys.argv[1:]
    S = scenes()
    if args != ["--vo"]:
        film(S, NAME, "PRYSM  ·  introducing Prysm", only=args, ver="1.4")
    if (OUT / f"{NAME}.mp4").exists():
        voice_over()
