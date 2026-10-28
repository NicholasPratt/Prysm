#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What's new in Prysm 1.4: the clap and the kits, parameter locks, the mod matrix, MOD FX, CRUSH and UNDO, the
looper and the LIVE screen's new mutes, shown on a 122 BPM deep house track (A minor: Am9, Fmaj9 stabs over an FM bass).
Made for small speakers too: the bass is FM (its harmonics carry where a phone has no low end), played in the second
octave, DIRECT past the filter; the chords sit in the middle of the keys, a high-pass under them. Captions only.

    tools/films/whatsnew14.py           -> takes/whatsnew14/whatsnew14.mp4, .srt, chapters.txt
    tools/films/whatsnew14.py 03-locks  film only that scene again (the others as filmed before), then join"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, Seq, film, info, reading_ms

NB = " "
KICK, SNARE, HAT, CLAP = range(4)
HIT, ACC = 1, 2
# the FM-1's keys (0 = F3): on LIVE the first 8 white keys are the slots, the first 4 black keys the patterns, the
# top white keys the mutes (G5: all on); on the DRUM page C#4 picks the clap, the white keys are the steps
SLOT_KEY = [0, 2, 4, 6, 7, 9, 11, 12]
PAT_KEY = [1, 3, 5, 8]
M_KICK, M_SNARE, M_HAT, M_CLAP, M_OSC1, M_OSC2, M_NOISE, M_ALL = 14, 16, 18, 19, 21, 23, 24, 26
STEP_KEY = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
CLAP_KEY = 8

# ---- the sound (each byte set as its knob would set it: the firmware's own knob tables)

def sound(base=None, knobs=()):
    """A patch: the default (or base) with knobs set, {(page, row, knob 1 .. 4): value}."""
    I = info()
    p = bytearray(base or I["init"])
    for (page, row, k), v in dict(knobs).items():
        p[I["knobs"][(page, row, k - 1)][0]] = v
    return bytes(p)


BPM = 122
TRACK = sound(knobs={
    # OSC 1, the bass: a sine FM'd by a sine x1 and a little x2, the brightness falling over each note
    ("OSC1", "RATIO", 1): 1, ("OSC1", "RATIO", 2): 1, ("OSC1", "RATIO", 3): 3,
    ("OSC1", "ENV", 1): 0, ("OSC1", "ENV", 2): 52, ("OSC1", "ENV", 3): 0, ("OSC1", "ENV", 4): 38,
    ("OSC1", "MIX", 1): 84, ("OSC1", "MIX", 2): 42, ("OSC1", "MIX", 3): 127,
    # OSC 2, the chords: a saw, a touch of FM x2 on the attack
    ("OSC2", "RATIO", 2): 3, ("OSC2", "ENV", 2): 40, ("OSC2", "MIX", 1): 26,
    ("MIX", "OSC", 1): 100, ("MIX", "OSC", 2): 112, ("MIX", "ROUTE", 1): 1,   # the bass DIRECT: past the filter
    ("MIX", "SPLIT", 1): 55,                     # G3: the bass below, the chords from it up
    ("FILTER", "FILTER", 1): 70, ("FILTER", "FILTER", 2): 52, ("FILTER", "FILTER", 3): 84,
    ("FILTER", "TYPE", 2): 28, ("FILTER", "TYPE", 3): 22,   # drive; the high-pass: no mud under the chords
    ("FENV", "ENV", 1): 0, ("FENV", "ENV", 2): 58, ("FENV", "ENV", 3): 22, ("FENV", "ENV", 4): 48,
    ("AMP", "ENV", 1): 0, ("AMP", "ENV", 2): 64, ("AMP", "ENV", 3): 46, ("AMP", "ENV", 4): 50,
    ("ARP", "TIME", 1): BPM - 40, ("ARP", "TIME", 3): 20,   # 122 BPM, a little swing
    ("CHO", "CHORUS", 3): 38,
    ("DLY", "DELAY", 1): 63, ("DLY", "DELAY", 2): 55, ("DLY", "DELAY", 3): 42, ("DLY", "D.MOD", 4): 55,   # 370 ms:
    ("REV", "REVERB", 1): 82, ("REV", "REVERB", 2): 72, ("REV", "REVERB", 3): 24,                       # a dotted 1/8
})

AM9, FMAJ9 = (60, 64, 71), (60, 64, 67)
CUT_LOCKS = {3: 62, 7: 86, 10: 72, 14: 66, 19: 66, 23: 90, 26: 76, 30: 108}   # step: the chord's cutoff


def drums(q, p, clap16=False):
    for b in (0, 16):
        q.drum(p, KICK, [b + 1, b + 5, b + 9, b + 13]).drum(p, CLAP, [b + 5, b + 13])
        q.drum(p, HAT, [b + 2, b + 6, b + 10, b + 14, b + 16]).drum(p, HAT, [b + 3, b + 7, b + 11, b + 15], ACC)
    if clap16:
        q.drum(p, CLAP, [16])


def bass(q, p):
    for k, n, st, acc in ((3, 45, 2, 0), (7, 45, 1, 0), (8, 52, 1, 0), (11, 45, 2, 1), (14, 48, 1, 0), (16, 43, 1, 0),
                          (19, 41, 2, 0), (23, 41, 1, 0), (24, 48, 1, 0), (27, 41, 2, 1), (32, 43, 1, 0)):
        q.note(p, k, n, steps=st, accent=bool(acc))


def kit(q):
    q.raw("seqs.dr_kit = 1;", "seqs.dr_lvl[0] = 112;", "seqs.dr_tune[0] = 28;", "seqs.dr_dec[0] = 104;",
          "seqs.dr_lvl[2] = 84;", "seqs.dr_lvl[3] = 104;", "seqs.dr_drive = 48;", "seqs.dr_rev = 10;")


def track_seq(clap16=False, lock14=66, cur=0):
    """P1: the track (32 steps: a bar of Am9, a bar of Fmaj9; the chords' cutoff locked step by step, the delay's send
    locked to 0 but for the last stab); P2: the drums and the bass alone (the looper's)."""
    q = Seq()
    drums(q, 0, clap16)
    bass(q, 0)
    for k, st, ch in ((3, 2, AM9), (7, 1, AM9), (10, 2, AM9), (14, 2, AM9),
                      (19, 2, FMAJ9), (23, 1, FMAJ9), (26, 2, FMAJ9), (30, 3, FMAJ9)):
        for lane, n in enumerate(ch):
            q.note(0, k, n, lane=1 + lane, steps=st)
    locks = {**CUT_LOCKS, 14: lock14}
    q.raw("seqs.pat[0].len = 32;", "seqs.pat[0].lk_dest[0] = LK_CUTOFF;", "seqs.pat[0].lk_dest[1] = LK_DSEND;")
    q.raw(*[f"seqs.pat[0].lk[0][{k - 1}] = {v + 1};" for k, v in locks.items()])
    q.raw(*[f"seqs.pat[0].lk[1][{k}] = {128 if k in (29, 30) else 1};" for k in range(32)])
    drums(q, 1, clap16)
    bass(q, 1)
    q.raw("seqs.pat[1].len = 32;", "seqs.pat[1].lk_dest[0] = LK_DSEND;")
    q.raw(*[f"seqs.pat[1].lk[0][{k}] = 21;" for k in range(32)])   # (a little echo on all: the looped chords')
    kit(q)
    q.raw(f"seqs.cur = {cur};")
    return q


STEP = 60000 / BPM / 4                          # a 1/16, ms
BAR = 16 * STEP


class Clock:
    """The bar lines of a sequencer started (PLAY) at t0: for presses timed to the music."""

    def __init__(self, s, step=STEP):
        self.s, self.t0, self.step = s, s.t, step

    def at(self, bar, step=1):
        return self.t0 + round((16 * bar + step - 1) * self.step)

    def until(self, bar, step=1, early=0):      # time on to there (never back)
        self.s.t = max(self.s.t, self.at(bar, step) - early)
        return self.s

    def next_bar(self, early=0, even=False):    # the first bar line still ahead (even: a pattern's first, 2 bars)
        n = 0
        while self.at(n) - early < self.s.t or (even and n % 2):
            n += 1
        return n

    def hold(self, early=0, even=False):        # the caption read, then on to the next bar line: its number
        t0, a, b, _ = self.s.caps[-1]
        self.s.t = max(self.s.t, t0 + reading_ms(a, b))
        n = self.next_bar(early, even)
        self.until(n, early=early)
        return n


def scenes():
    S = []
    one = lambda p, q: {1: (bytes(p), q)}

    # 1 ---- the track, playing on LIVE
    s = Scene("01-intro", "1 · Prysm 1.4", splash=True, tail=2, slots=one(TRACK, track_seq(True, 120)), load=1)
    s.go("LIVE").press("PLAY", 0)
    c = Clock(s)
    s.caption("What's new in 1.4",
              "Parameter locks, a mod matrix, a MIDI looper, undo, a clap and two new drum kits, a phaser, a flanger "
              "and a bit crusher.")
    c.hold(even=True)
    s.caption("A deep house track",
              "Made on the FM-1 for this video, at 122" + NB + "BPM. The bass is FM, rich in harmonics, so it comes "
              "through on small speakers too.")
    c.hold(even=True)
    c.until(c.next_bar(even=True) + 2)
    s.press("PLAY", 0)
    S.append(s)

    # 2 ---- the clap and the kits
    s = Scene("02-drums", "2 · A clap and three kits", tail=2, slots=one(TRACK, track_seq()), load=1)
    s.go("DRUM").press("PLAY", 0)
    c = Clock(s)
    s.caption("Four drums",
              "The DRUM page has a fourth lane: CLAP, here on 2 and 4 with the kick on every beat and the hats "
              "around them.")
    c.hold()
    s.caption("The clap's own key",
              "C#4 picks the clap (and plays it). Then the white keys are its steps: G5, step" + NB + "16, a clap "
              "leading into the bar.")
    s.wait(300).key(CLAP_KEY, label="press C#4 (CLAP)").wait(600).key(STEP_KEY[15], label="press G5 (step 16)")
    c.hold(even=True)
    s.caption("BOOM",
              "The new KIT row: three kits, each slot keeping its own. BOOM is deep: a long, low kick and a fat clap.")
    s.row("KIT").twist(1, 2, "KIT")
    c.hold(even=True)
    c.until(c.next_bar(even=True), early=300)
    s.caption("LO-FI",
              "LO-FI is the kit from 1.3, now with a clap: 8 bits at a low rate, played back as an old sampler "
              "would.")
    s.twist(1, -4, "KIT")
    c.hold(even=True)
    s.caption("TR",
              "TR is clean and bright: a punchy kick, metallic hats. Back to it for this track.")
    c.until(c.next_bar(even=True), early=300)
    s.twist(1, 2, "KIT")
    c.hold(even=True)
    s.press("PLAY", 0)
    S.append(s)

    # 3 ---- parameter locks
    s = Scene("03-locks", "3 · Parameter locks", tail=4, slots=one(TRACK, track_seq(True)), load=1)
    s.go("SEQ").press("PLAY", 0)
    c = Clock(s)
    s.caption("Parameter locks",
              "The SEQ page's new LOCK row: each pattern has two lock lanes, a parameter each. LANE1 here is the "
              "CUTOFF.")
    s.row("LOCK")
    c.hold(even=True)
    s.caption("A brightness each",
              "Every chord stab has its cutoff locked, so each plays its own brightness. A locked step has a mark in "
              "its corner.")
    c.hold(even=True)
    s.caption("Lock a step",
              "Pick the step on the STEP row (KNOB" + NB + "1: step 14), then turn VAL1 on the LOCK row: that stab "
              "opens up, and only that one.")
    s.row("STEP").twist(1, 13, "the step", 1200).row("LOCK")
    c.until(c.next_bar(even=True), early=200)
    s.twist(2, 54, "VAL1", 3800)
    c.hold(even=True)
    s.caption("A delay throw",
              "LANE2 is DLY" + NB + "SND, locked to 0 on every step but the last stab's: only that one goes into "
              "the delay. One echo, at the end of the phrase.")
    c.hold(even=True)
    c.until(c.next_bar(even=True) + 2)
    s.press("PLAY", 0)
    S.append(s)

    # 4 ---- the mod matrix
    q = track_seq(True, 120)
    s = Scene("04-matrix", "4 · The mod matrix", after=S[-1], tail=2, slots=one(S[-1].patch, q), load=1)
    s.go("MATRIX").press("PLAY", 0)
    c = Clock(s)
    s.caption("The mod matrix",
              "LFO three times: four more routes, any source onto any of the LFO's destinations, an amount either "
              "way.")
    c.hold()
    s.caption("RANDOM onto CUTOFF",
              "MOD" + NB + "1: RANDOM, a new value at every note, onto the CUTOFF. Each stab a little brighter or "
              "darker: no two bars quite alike.")
    s.turn(1, 8, 1200).turn(3, 80, 1200)
    c.hold(even=True)
    s.caption("LFO 2 onto MOD1",
              "MOD" + NB + "2: LFO" + NB + "2 onto MOD1, the FM's depth. The bass and the chords breathe, slowly, "
              "brighter and back.")
    s.row("MOD 2").turn(1, 2, 600).turn(2, 7, 900).turn(3, 88, 1200)
    c.hold(even=True)
    c.until(c.next_bar(even=True) + 2)
    s.press("PLAY", 0)
    S.append(s)

    # 5 ---- MOD FX
    s = Scene("05-modfx", "5 · Phaser and flanger", after=S[-1], tail=3, slots=one(S[-1].patch, q), load=1)
    s.go("MFX").press("PLAY", 0)
    c = Clock(s)
    s.caption("MOD FX",
              "FX four times: the new MOD" + NB + "FX page. TYPE PHASER, a slow RATE: the chords sweep, the classic "
              "deep house whoosh.")
    s.turn(1, 1, 600).turn(2, 22, 900)
    c.hold(even=True)
    s.caption("MANUAL",
              "The TONE row's MANUAL sets where it sweeps: low, deep; high, thin. FDBK deepens the notches.")
    s.row("TONE").turn(1, 40, 2000).turn(2, 100, 1200)
    c.hold(even=True)
    s.caption("FLANGER",
              "TYPE FLANGER: a short delay swept against the sound, a jet. With the feedback up it rings.")
    s.row("MOD FX").turn(1, 2, 600)
    c.hold(even=True)
    s.caption("Back to the phaser",
              "The phaser for this track, the feedback back down. It works on the synth's voices, before the "
              "chorus; the drums stay dry.")
    s.turn(1, 1, 600).row("TONE").turn(2, 80, 900)
    c.hold(even=True)
    s.press("PLAY", 0)
    S.append(s)

    # 6 ---- CRUSH and UNDO
    s = Scene("06-crush", "6 · CRUSH and UNDO", after=S[-1], tail=3, slots=one(S[-1].patch, q), load=1)
    s.go("CRUSH").press("PLAY", 0)
    c = Clock(s)
    s.caption("CRUSH",
              "FX five times. BITS and RATE: fewer bits, a lower sample rate. 11 bits at 11" + NB + "kHz: a little "
              "old-sampler grit.")
    s.turn(1, 40, 1200).turn(2, 16, 1000)
    c.hold(even=True)
    keep = bytes(s.patch)
    s.caption("Too far",
              "Then 4 bits at 2" + NB + "kHz: broken, like a toy. Too much for this track.")
    s.wait(2500).turn(1, 110, 1500).turn(2, 90, 1200)
    c.hold(even=True)
    s.caption("UNDO",
              "SAVE, then SELECT to UNDO: the screen says what it takes back. SAVE does it: the crush as it was. "
              "SAVE again would redo.")
    s.press("SAVE", 900).row("UNDO").wait(1200).press("SAVE", 0).sound(keep)
    c.hold(even=True)
    s.caption("One step back",
              "A change is a knob turned after a pause, or a PASTE, CLEAR, LOAD or INIT. It undoes the sound and the "
              "sequencer.")
    c.hold(even=True)
    s.press("PLAY", 0)
    S.append(s)

    # 7 ---- the looper (P2: the drums and the bass)
    s = Scene("07-loop", "7 · The looper", after=S[-1], tail=3, slots=one(S[-1].patch, track_seq(True, 120, cur=1)),
              load=1)
    s.go("LOOP").press("PLAY", 0)
    c = Clock(s)
    s.caption("A MIDI looper",
              "SEQ three times: the LOOP page. It records the notes you play, in time with the sequencer. Here P2: "
              "just the drums and the bass.")
    s.twist(2, 2, "QUANT", 300)
    c.hold(even=True)
    s.caption("Record",
              "REC: recording starts on the next bar line, for two bars (BARS). QUANT 1/16 puts each note on the "
              "nearest 1/16.")
    n = c.next_bar(400, even=True)
    c.until(n, early=400)
    s.press("REC", 0)
    riff = ((1, 3, AM9), (4, 2, AM9), (7, 3, AM9), (11, 4, (62, 64, 71)),
            (17, 3, FMAJ9), (20, 2, FMAJ9), (23, 3, FMAJ9), (27, 4, (60, 65, 69)))
    for k, st, ch in riff:
        s.t = c.at(n, k) + 12
        s.on(*ch).wait(round(st * STEP) - 40).off(*ch)
    c.until(n + 2)
    s.caption("Round and round",
              "After two bars it plays back, still recording (OVERDUB): what you play now is added. REC ends it: "
              "PLAYING.")
    s.wait(400).press("REC", 0)
    c.hold(even=True)
    s.caption("Notes, not audio",
              "The loop plays the sound as it is now: turn the cutoff (PRESETS) and the looped chords follow.")
    s.turn("cut", 40, 3000).turn("cut", 92, 3000).turn("cut", 70, 1500)
    c.hold(even=True)
    s.press("PLAY", 0)
    S.append(s)

    # 8 ---- LIVE: the mutes
    s = Scene("08-live", "8 · LIVE mutes", after=S[-1], tail=5, slots=one(S[-1].patch, track_seq(True, 120)), load=1)
    s.go("LIVE").press("PLAY", 0)
    c = Clock(s)
    s.caption("The mutes, moved",
              "On LIVE the mutes are now the top white keys, the drums together: G4 to C5 the kick, snare, hats, "
              "clap; D5, E5 the oscillators; F5 the noise.")
    n = c.hold(early=300, even=True)
    s.caption("A breakdown",
              "The kick, the hats and the clap out (their keys flash), the cutoff down: the chords and the bass "
              "alone.")
    s.key(M_KICK, M_HAT, M_CLAP, gap=90, label="press G4, B4, C5 (KICK, HATS, CLAP)")
    s.turn(1, 45, 2500)
    n = c.hold(even=True)
    s.caption("G5: all back on",
              "G5 turns every mute back on at once: the drop. It is lit while anything is muted.")
    c.until(n + 1, 5)
    s.turn(1, 70, 1500, block=False)
    c.until(n + 2, early=30)
    s.key(M_ALL, label="press G5 (all on)")
    n = c.hold(even=True)
    s.caption("Bass out",
              "D5 mutes OSC" + NB + "1: with the split, the whole bass. Back on for the next bar.")
    c.until(n + 1, early=30)
    s.key(M_OSC1, label="press D5 (OSC 1)")
    c.until(n + 2, early=30)
    s.key(M_OSC1, label="press D5 (back on)")
    n = c.hold(even=True)
    s.caption("Only on LIVE",
              "The mutes hold only while you are on the LIVE screen: leave it, and everything plays again.")
    c.hold(even=True)
    c.until(c.next_bar(even=True) + 2)
    s.press("PLAY", 0)
    S.append(s)

    # 9 ---- get it
    s = Scene("09-get", "9 · Get it", tail=4, slots=one(S[-1].patch, track_seq(True, 120)), load=1)
    s.go("DRUM").press("PLAY", 0)
    c = Clock(s)
    s.caption("Install 1.4",
              "Free, from the web installer at nicholaspratt.github.io/Prysm (Chrome or Edge). Your sounds and "
              "patterns come along.")
    c.hold(even=True)
    s.caption("Also new",
              "The browser editor has the clap and the KIT row too. The manual has the rest: every matrix source, the "
              "FX chain, the looper's overdub.")
    c.hold(even=True)
    c.until(c.next_bar(even=True) + 2)
    s.press("PLAY", 0)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "whatsnew14", "PRYSM  ·  what's new in 1.4", only=sys.argv[1:], ver="1.4")
