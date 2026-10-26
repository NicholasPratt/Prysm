#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What's new in Prysm 0.9, in about four minutes: chords in the sequencer, real-time recording with the metronome
and the count-in, the self-oscillating filter, the envelope pages and mod envelope 2, PIT and FINE. From the default
sound. Captions only.

    tools/films/whatsnew09.py           -> takes/whatsnew09/whatsnew09.mp4, .srt, chapters.txt
    tools/films/whatsnew09.py 03-filter film only that scene again (the others as filmed before), then join"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, film

NB = " "
STEP = 125                                      # a 1/16 at the default 120 BPM, ms
MIX = 2
ST = lambda n: 24 + n
CENTS = lambda c: 64 + round(c * 63 / 50)


def scenes():
    S = []

    # 1 ---- intro
    s = Scene("01-intro", "1 · Prysm 0.9", splash=True, tail=1)
    s.caption("What's new in 0.9",
              "A sequencer that plays chords and records as you play, a filter that sings on its own, a second mod "
              "envelope, and envelopes with pages of their own.")
    s.wait(400).on(48, 55, 64).wait(5500).off(48, 55, 64)
    S.append(s)

    # 2 ---- the sequencer (one scene: the patterns do not carry from one scene to the next)
    s = Scene("02-seq", "2 · Chords and live recording", tail=2)
    s.caption("Chords in a step",
              "Each step can now hold up to four notes. Press REC, and hold keys together: they all go into the "
              "step, and the cursor moves on when you let go.")
    s.press("REC", 700)
    for i, chord in enumerate([(48, 55, 64), (45, 52, 60), (41, 53, 57), (43, 50, 59)]):
        s.on(*chord).wait(700).off(*chord).wait(300)
        if i < 3:
            s.twist(1, 3, "STEP", 400)          # (the cursor: three steps on, to the next beat)
    s.press("REC", 600)
    s.caption("Play it",
              "PLAY runs the pattern: four chords, one on each beat. Each of a chord's notes is a voice of its own.")
    s.press("PLAY", 300).wait(4500)
    s.caption("The CHORD row",
              f"On the CHORD row, N2, N3 and N4 show a step's other notes (KNOB{NB}1 on the STEP row picks the "
              "step). The pattern marks them with notches.")
    s.twist(1, -13, "STEP", 1200).row("CHORD").wait(3500).press("PLAY", 600)   # (step 1: the C chord)
    s.caption("The metronome",
              "The new CLICK row sets the metronome: ON, or REC (only while recording). It ticks every beat, higher "
              "on each bar's first.")
    s.row("CLICK").twist(1, 1, "CLICK", 300).press("PLAY", 8000).press("PLAY", 600)
    s.caption("Count-in, then record",
              "Press REC, then PLAY: a bar of ticks counts you in. Then play over the chords: each note lands on the "
              "nearest step, at its velocity.")
    s.press("REC", 400)
    t0 = s.t
    s.press("PLAY", 0)
    bar = t0 + 16 * STEP                       # the count-in is one bar (16 steps); then step 1
    for k, n in [(0, 36), (3, 36), (6, 48), (8, 45), (10, 45), (12, 41), (14, 43)]:
        s.t = bar + k * STEP
        s.on(n).wait(90).off(n)
    s.t = bar + 16 * STEP
    s.caption("Overdub",
              "Recording adds to what is there: the chords stay, and the bass goes in beside them. REC again stops "
              "recording, and the pattern plays on.")
    s.press("REC", 2 * 16 * STEP).press("PLAY", 600)
    s.row("CLICK").twist(1, -1, "CLICK", 300)
    S.append(s)

    # 3 ---- the filter
    s = Scene("03-filter", "3 · A filter that sings", tail=2)
    s.caption("Self-oscillation",
              "Turn RES to the top: from 122 up the filter rings on its own, a pure sine at the cutoff, as on an "
              "analog synth.")
    s.go("FILTER").turn(3, 64, 800).turn(1, 60, 800).on(57).turn(2, 127, 4000).wait(1500)
    s.turn(1, 90, 3000).turn(1, 50, 3000).off(57)
    s.caption("Just the filter",
              "Turn both oscillators down (MIX page): the filter still sings, on its own. It holds the same level at "
              "every cutoff.")
    s.go("MIX").row("OSC").turn(1, 0, 1200).turn(2, 0, 1200).on(57).wait(3000).off(57)
    s.caption("Play it from the keys",
              "With KEY at 127 the sine follows the keyboard in tune. At a CUT of about 47 each key sings near its "
              "own note. A whistling lead from nothing but a filter.")
    s.go("FILTER").turn(4, 127, 1200).turn(1, 47, 1200)
    s.play([57, 60, 64, 62, 60, 57, 55, 57], dur=380, gap=420).wait(400)
    S.append(s)

    # 4 ---- envelopes
    s = Scene("04-env", "4 · Envelope pages", tail=2)
    s.caption("ENV, four pages",
              "ENV now steps through four pages: AMP, then F.ENV (the filter's envelope, off the FILTER page), "
              "M.ENV 1 and the new M.ENV 2.")
    s.press("ENV", 1600).press("ENV", 1600).press("ENV", 1600).press("ENV", 1600)
    s.caption("Mod envelope 2",
              "M.ENV 2 is a second mod envelope, with its own destination and amount. Here, on CUTOFF, with LOOP on: "
              "a filter that pulses while the chord is held.")
    s.row("DEST").turn(2, 10, 1500).turn(3, 100, 1200).row("ENV").turn(2, 50, 800)
    s.row("DEST").on(48, 55, 64).turn(1, 1, 400).wait(5000)
    s.caption("Two at once",
              "M.ENV 1 is still there beside it: the two can share a destination (they add up) or move different "
              "things.")
    s.wait(4500).off(48, 55, 64).turn(1, 0, 400).turn(3, 64, 800)
    S.append(s)

    # 5 ---- PIT and FINE
    s = Scene("05-pitch", "5 · Pitch for MIX and RING", tail=2)
    s.caption("RATIO becomes PIT",
              "A modulator in MIX or RING is heard, so its RATIO knob becomes PIT1 or PIT2: semitones, -24 to +24. "
              "+12 adds a layer an octave up.")
    s.go("OSC1").row("MODE").turn(1, MIX, 600).row("MIX").turn(1, 110, 800).row("RATIO").on(45, 57).turn(2, ST(12), 1500)
    s.wait(1500).turn(2, ST(7), 800).wait(2500).off(45, 57)
    s.caption("FINE1, FINE2",
              "The MODE row's new FINE knobs detune each modulator by up to 50 cents, in any mode: a beating layer in "
              "MIX, a slow shimmer in FM.")
    s.row("MODE").on(45, 57).turn(3, CENTS(10), 3000).wait(3500).off(45, 57)
    s.caption("The deep dive",
              "PIT, FINE and the new M1PITCH and M2PITCH destinations get a full tour in the video Oscillators in "
              "depth.")
    s.play([(45, 57), (48, 60), (43, 55)], dur=1500, gap=1700)
    S.append(s)

    # 6 ---- get it
    s = Scene("06-get", "6 · Get it", tail=4)
    s.caption("Install 0.9",
              "Free, from the web installer at nicholaspratt.github.io/Prysm (Chrome or Edge). Your saved sounds and "
              "patterns come along.")
    s.on(48, 55, 64).wait(4000).off(48, 55, 64)
    s.caption("The editor and the plugin",
              "The browser editor and the CLAP plugin know everything new, and stay in step with an FM-1 running "
              "0.9.")
    s.play([(45, 52, 60), (41, 53, 57), (43, 50, 59), (48, 55, 64)], dur=1200, gap=1400)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "whatsnew09", "PRYSM  ·  what's new in 0.9", only=sys.argv[1:])
