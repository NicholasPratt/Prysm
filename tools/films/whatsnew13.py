#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What's new in Prysm 1.3: the keyboard split, two sequencer parts (a bass recorded, a lead over it), a sequencer per
patch, and the LIVE screen, shown by playing out a 140 BPM half-time track: a breakdown, a build on the mutes, the drop
(a wobble bass: the LFO on the cutoff and FM, its rate swept by M.ENV 1 on every note), pattern changes, a bass
drop-out, a second wobble that speeds up. Captions only.

    tools/films/whatsnew13.py           -> takes/whatsnew13/whatsnew13.mp4, .srt, chapters.txt
    tools/films/whatsnew13.py 04-live   film only that scene again (the others as filmed before), then join"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, Seq, film, info, reading_ms

NB = " "
KICK, SNARE, HAT = range(3)
HIT, ACC, FLAM = 1, 2, 3
# the FM-1's keys (0 = F3): on LIVE the first 8 white keys are the slots, the first 4 black keys the patterns, the
# black keys above middle F the mutes
SLOT_KEY = [0, 2, 4, 6, 7, 9, 11, 12]
PAT_KEY = [1, 3, 5, 8]
M_KICK, M_SNARE, M_HAT, M_OSC1, M_OSC2, M_NOISE = 13, 15, 17, 20, 22, 25

# ---- the sounds (each byte set as its knob would set it: the firmware's own knob tables)

SINE, TRI, SAWUP, SAWDN, SQUARE, SH = range(6)
D_MOD1, D_MPITCH, D_CUTOFF, D_LFORATE = 7, 9, 10, 13
BASIC, HARM, PULSE, VOCAL, DIGI, CMPLX = range(6)
SAW, SQ = 73, 91                                # (BASIC's wave positions)


def sound(base=None, **knobs):
    """A patch: the default (or base) with knobs set, as page_row_knob=value (knob 1 .. 4), e.g. FILTER_FILTER_1=30."""
    I = info()
    p = bytearray(base or I["init"])
    for name, v in knobs.items():
        page, row, k = name.rsplit("_", 2)
        p[I["knobs"][(page, row.replace("DOT", "."), int(k) - 1)][0]] = v
    return bytes(p)


BPM140 = 100                                    # (BPM = 40 + the knob)
WOBBLE = sound(
    OSC1_WAVE_1=BASIC, OSC1_WAVE_2=SAW, OSC1_WAVE_3=BASIC, OSC1_WAVE_4=SQ,   # a saw, FM'd by a square
    OSC1_RATIO_2=1, OSC1_RATIO_3=0, OSC1_MIX_1=55, OSC1_MIX_2=25, OSC1_MIX_3=127,
    OSC2_WAVE_2=40, OSC2_RATIO_2=3, OSC2_MIX_1=40, OSC2_ENV_1=0, OSC2_ENV_2=45,   # the stabs: an FM pluck
    MIX_OSC_1=127, MIX_OSC_2=80, MIX_ROUTE_2=1,  # OSC 2 DIRECT: past the wobbling filter
    MIX_SPLIT_1=60, MIX_GLIDE_1=20, MIX_GLIDE_2=1,
    FILTER_FILTER_1=50, FILTER_FILTER_2=82, FILTER_FILTER_3=64, FILTER_TYPE_1=1, FILTER_TYPE_2=60,
    AMP_ENV_1=0, AMP_ENV_2=60, AMP_ENV_3=120, AMP_ENV_4=25,
    LFO_LFO_1=SINE, LFO_LFO_2=74, LFO_LFO_4=1,  # 2.3 Hz: a wobble a beat at 140
    LFO_DEST_1=D_CUTOFF, LFO_DEST_2=108, LFO_DEST_3=D_MOD1, LFO_DEST_4=100,
    MENV_ENV_1=0, MENV_ENV_2=110, MENV_ENV_3=0, MENV_ENV_4=40,   # every note: the LFO fast, slowing to the beat
    MENV_DEST_1=0, MENV_DEST_2=D_LFORATE, MENV_DEST_3=112,
    ARP_TIME_1=BPM140,
    DLY_DELAY_1=54, DLY_DELAY_2=45, DLY_DELAY_3=22, DLY_DELAY_4=1, REV_REVERB_3=18)
WOBBLE2 = sound(
    WOBBLE,
    OSC1_WAVE_1=DIGI, OSC1_MIX_1=70, FILTER_FILTER_1=36, FILTER_FILTER_2=95, LFO_DEST_2=118,
    LFO_LFO_2=99, LFO_DEST_3=D_MPITCH, LFO_DEST_4=92,   # 9.3 Hz (a 1/16), the FM's pitch talking
    MENV_ENV_2=114, MENV_DEST_3=6)              # the envelope the other way: slow, speeding up
BREAK = sound(
    AMP_ENV_1=30, AMP_ENV_4=85, FILTER_FILTER_1=70, CHO_CHORUS_3=45,
    DLY_DELAY_1=54, DLY_DELAY_3=35, REV_REVERB_3=55, ARP_TIME_1=BPM140)


def wobble_seq():
    q = Seq()
    for p in range(4):                          # the beat, half time: the kick on 1, the snare on 3
        q.drum(p, KICK, [1, 11] if p != 2 else [1]).drum(p, SNARE, [9])
    q.drum(0, HAT, [3, 7, 11]).drum(0, HAT, [15], ACC)
    q.drum(1, HAT, [1, 3, 5, 6, 7, 11, 13, 14]).drum(1, HAT, [15], ACC).drum(1, KICK, [4])
    q.drum(3, HAT, [1, 3, 5, 7]).drum(3, SNARE, [13, 14]).drum(3, SNARE, [15, 16], FLAM)
    # P1: a long note, then shorter ones: each restarts the wobble, fast to slow
    q.note(0, 1, 40, steps=6).note(0, 7, 40, steps=2).note(0, 9, 43, steps=2).note(0, 11, 40, steps=3, accent=True)
    q.note(0, 14, 52).note(0, 15, 50, steps=2, slide=True)
    q.note(0, 3, 64, lane=1).note(0, 3, 71, lane=2).note(0, 13, 67, lane=1).note(0, 13, 74, lane=2)
    # P2: busier
    q.note(1, 1, 40, steps=3).note(1, 4, 40, steps=3).note(1, 7, 47, steps=2).note(1, 9, 40, steps=4, accent=True)
    q.note(1, 13, 43, steps=2).note(1, 15, 38, steps=2, slide=True)
    q.note(1, 3, 64, lane=1).note(1, 3, 71, lane=2).note(1, 7, 66, lane=1).note(1, 7, 74, lane=2)
    # P3: two long wobbles, a chord held over them
    q.note(2, 1, 40, steps=8).note(2, 9, 36, steps=8)
    for i, n in enumerate((64, 67, 71)):
        q.note(2, 1, n, lane=1 + i, steps=16)
    # P4: the fill, rising
    for i, n in enumerate((40, 43, 45, 47)):
        q.note(3, 1 + 4 * i, n, steps=4)
    q.raw("seqs.dr_drive = 40;", "seqs.dr_crush = 18;", "seqs.dr_rev = 20;", "seqs.dr_tune[0] = 21;",
          "seqs.dr_lvl[0] = 120;")
    return q


def wobble2_seq():                              # the second drop: triplet-ish, lower
    q = Seq()
    for p in range(4):
        q.drum(p, KICK, [1, 8, 11]).drum(p, SNARE, [9]).drum(p, HAT, [3, 6, 12, 14]).drum(p, HAT, [15], ACC)
    q.note(0, 1, 38, steps=3).note(0, 4, 38, steps=3).note(0, 7, 45, steps=2).note(0, 9, 38, steps=5, accent=True)
    q.note(0, 14, 41, steps=3, slide=True)
    q.note(0, 1, 62, lane=1).note(0, 1, 69, lane=2).note(0, 7, 65, lane=1).note(0, 7, 72, lane=2)
    q.copy(0, 1).copy(0, 2).copy(0, 3)
    q.raw("seqs.dr_drive = 55;", "seqs.dr_crush = 30;", "seqs.dr_rev = 20;", "seqs.dr_tune[0] = 20;",
          "seqs.dr_lvl[0] = 120;")
    return q


def break_seq():                                # the breakdown: two chords a bar, a soft beat, in a song of P1, P2
    q = Seq()
    for p, (a, b) in enumerate((((52, 55, 59, 64), (48, 55, 60, 64)), ((43, 55, 59, 62), (50, 54, 57, 62)))):
        for lane, n in enumerate(a):
            q.note(p, 1, n, lane=lane, steps=8)
        for lane, n in enumerate(b):
            q.note(p, 9, n, lane=lane, steps=8)
        q.drum(p, KICK, [1, 11]).drum(p, SNARE, [9]).drum(p, HAT, [3, 7, 11, 15])
    q.raw("seqs.mode = 1;", "seqs.song_len = 2;", "seqs.dr_rev = 50;", "seqs.dr_lvl[0] = 100;")
    return q


SLOTS = {1: (WOBBLE, wobble_seq()), 2: (BREAK, break_seq()), 3: (WOBBLE2, wobble2_seq())}

STEP = 60000 / 140 / 4                          # a 1/16 at 140 BPM, ms
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

    def next_bar(self, early=0):                # the first bar line still ahead (with room to act before it)
        n = 0
        while self.at(n) - early < self.s.t:
            n += 1
        return n

    def hold(self, early=0):                    # the caption read, then on to the next bar line: its number
        t0, a, b, _ = self.s.caps[-1]
        self.s.t = max(self.s.t, t0 + reading_ms(a, b))
        n = self.next_bar(early)
        self.until(n, early=early)
        return n


def scenes():
    S = []

    # 1 ---- intro: the drop, playing
    s = Scene("01-intro", "1 · Prysm 1.3", splash=True, tail=2, slots=SLOTS, load=1)
    s.go("MENV").press("PLAY", 0)               # (M.ENV 1's picture: the envelope, the LFO under it, moved)
    c = Clock(s)
    s.caption("What's new in 1.3",
              "A keyboard split, two sequencer parts, a sequencer for every patch, and a LIVE screen to play it "
              "all out.")
    c.hold()
    s.press("PLAY", 0)
    S.append(s)

    # 2 ---- the split (from the default sound: follow along)
    s = Scene("02-split", "2 · The keyboard split", tail=2.5)
    s.caption("Two sounds on one keyboard",
              "The MIX page has a new SPLIT row. Below the split key only OSC 1 plays; from it up, only OSC 2: a "
              "bass under a lead.")
    s.go("MIX").row("SPLIT").wait(1500)
    s.caption("LEARN",
              "Turn LEARN, then press a key: it becomes the split (it is not played). Or set it with KNOB 1. Here, "
              "C4.")
    s.twist(4, 2, "LEARN", 300).wait(800).on(60).wait(150).off(60)
    s.patch[info()["knobs"][("MIX", "SPLIT", 0)][0]] = 60
    s._how("press C4")
    s.wait(2000)
    s.caption("Each side its octave",
              "OCT1 and OCT2 move each side by octaves. OSC 1 an octave down: a bass in the middle of the keys, a "
              "chord over it.")
    s.turn(2, 1, 600)
    for i in range(2):
        s.play([48, 48, 51, 43], dur=330, gap=400, at=s.t)
        s.on(64, 67, 71).wait(1600).off(64, 67, 71)
        s.on(63, 67, 70).wait(1600).off(63, 67, 70)
    s.caption("Still four voices",
              "The four voices are shared by both sides, any mix of them. The split is saved with the patch, so "
              "every patch can have its own.")
    s.play([(48, 64, 67, 71), (44, 63, 67, 72), (46, 65, 70, 74), (43, 62, 67, 71)], dur=900, gap=1000)
    S.append(s)

    # 3 ---- two parts: a bass recorded, a lead over it (the default tempo: 120 BPM)
    st = 125
    s = Scene("03-parts", "3 · Two sequencer parts", after=S[-1], tail=2.5)
    s.go("SEQ")
    s.caption("Record the bass",
              "With the split on, the sequencer has two parts: the notes below the split, and those above. PLAY, "
              "then REC, and play the bass.")
    s.press("PLAY", 0)
    c = Clock(s, st)
    c.until(0, 9)
    s.press("REC", 0)
    for k, n in ((1, 48), (4, 48), (7, 51), (9, 43), (11, 46), (13, 48), (15, 51)):
        s.t = c.at(1, k) + 8
        s.on(n).wait(st * 2 - 30).off(n)
    c.until(2, early=40)
    s.press("REC", 0)
    s.caption("Overdub the lead",
              "REC again and play above the split: the lead goes in as part 2 while the bass plays on. Recording "
              "replaces only the part you play.")
    n = c.hold(250)
    s.press("REC", 0)
    for k, m in ((3, 72), (6, 75), (8, 79), (11, 77), (14, 75)):
        s.t = c.at(n, k) + 8
        s.on(m).wait(st * 2 - 30).off(m)
    c.until(n + 1, early=40)
    s.press("REC", 0)
    s.caption("MUTE1, MUTE2",
              "The new PARTS row mutes either part (MUTE1, MUTE2) or clears it (CLR1, CLR2). The lead alone, then "
              "the bass alone.")
    s.row("PARTS").twist(1, 2, "MUTE1", 300)
    n = c.next_bar(300)
    c.until(n + 2, early=300)
    s.twist(1, -2, "MUTE1", 300).twist(2, 2, "MUTE2", 300)
    c.until(n + 4, early=300)
    s.twist(2, -2, "MUTE2", 300)
    s.caption("Saved with the sound",
              "SAVE now stores the sequencer with the patch: every slot has its own patterns, song and drum kit.")
    s.press("SAVE", 300).row("SAVE").slot(4).press("SAVE", 1500)
    c.hold()
    s.press("PLAY", 300)
    S.append(s)

    # 4 ---- LIVE: three slots saved, each with its own sequencer; slot 2 (the breakdown) loaded
    s = Scene("04-live", "4 · The LIVE screen", tail=5, slots=SLOTS, load=2)
    s.caption("The LIVE screen",
              "HOME twice. Three patches here, each saved with its own patterns. Slot 2, a breakdown, is playing.")
    s.go("LIVE").press("PLAY", 0)
    c = Clock(s)
    c.hold()
    s.caption("A build",
              "The black keys above middle F are mutes: lit, on; flashing, muted. The kick and the hats out.")
    s.wait(300).key(M_KICK, M_HAT, gap=200, label="press F#4 (KICK), A#4 (HATS)")
    n = c.hold()
    s.caption("The drop, on the bar",
              "The first eight white keys are the patches. One waits for the bar line, then the sound and its "
              "sequencer change together.")
    s.wait(300).key(SLOT_KEY[0], label="press F3 (slot 1)")
    c.until(n + 1, early=150)
    s.key(M_KICK, M_HAT, gap=60, label="press F#4, A#4 (back on)").sound(WOBBLE)
    c.hold()
    s.caption("The wobble",
              "Slot 1's bass: LFO 1 on the cutoff and the FM, M.ENV 1 on the LFO's RATE. Each note's wobble starts "
              "fast and slows to the beat.")
    c.hold()
    s.caption("Patterns on the bar",
              "The first four black keys are the patterns, on the bar line too. P2: busier.")
    s.wait(300).key(PAT_KEY[1], label="press G#3 (P2)")
    n = c.hold()
    s.caption("Drop the bass",
              "C#5 mutes OSC 1. With the split, that is the whole bass: the drums and the stabs go on. Back on the "
              "next bar.")
    c.until(n + 1, early=120)
    s.key(M_OSC1, label="press C#5 (OSC 1)")
    c.until(n + 2, early=120)
    s.key(M_OSC1, label="press C#5 (back on)")
    n = c.hold()
    s.caption("A fill",
              "P4 is the fill: the bass climbing, snare flams at the end (new on the DRUM page: HIT after ACCENT). "
              "Then P1.")
    s.wait(300).key(PAT_KEY[3], label="press C#4 (P4)")
    c.until(n + 1, 5)
    s.key(PAT_KEY[0], label="press F#3 (P1)")
    c.hold()
    s.caption("Four knobs, your choice",
              "KNOB 1 to 4 can each be any parameter. As made: the cutoff, resonance, delay and reverb mix.")
    s.wait(200).turn(1, 70, 2400).turn(1, 26, 2400)
    n = c.next_bar(900)
    c.until(n, early=700)
    s.turn(3, 80, 250)
    c.until(n, 5)
    s.turn(3, 22, 500)
    c.hold()
    s.caption("Choose them",
              "SELECT picks a knob on the screen, PRESETS its parameter: the hint under the picture says so. Saved "
              "in the settings.")
    n = c.hold()
    s.caption("The second drop",
              "Slot 3: the envelope turned the other way, so the wobble starts slow and speeds up, the FM growling. "
              "Its own patterns again.")
    s.wait(300).key(SLOT_KEY[2], label="press A3 (slot 3)")
    c.until(n + 1)
    s.sound(WOBBLE2)
    n = c.hold()
    c.until(n + 2)
    s.caption("And out",
              "Back to slot 2 on the bar, then PLAY stops. The mutes are not saved: all on at every power-up.")
    s.wait(300).key(SLOT_KEY[1], label="press G3 (slot 2)")
    c.until(c.next_bar())
    s.sound(BREAK)
    c.hold()
    s.press("PLAY", 0)
    S.append(s)

    # 5 ---- get it
    s = Scene("05-get", "5 · Get it", tail=4, slots=SLOTS, load=1)
    s.go("LIVE").press("PLAY", 0)
    c = Clock(s)
    s.caption("Install 1.3",
              "Free, from the web installer at nicholaspratt.github.io/Prysm (Chrome or Edge). Your sounds and "
              "patterns come along.")
    c.hold()
    s.caption("Also new",
              "MIDI in on the TRS jack. The browser editor and the CLAP plugin have the split too.")
    c.hold()
    s.press("PLAY", 0)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "whatsnew13", "PRYSM  ·  what's new in 1.3", only=sys.argv[1:], ver="1.3")
