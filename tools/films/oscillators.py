#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Oscillators in depth: everything inside one of Prysm's oscillators, from a single sine to a stack of two chains.
From the default sound (a viewer can load it too): the chain of three operators, wavetables, FM and its ratios,
the modulator's own wave, mod 2, the modulators' envelopes, RING and MIX (with PIT and FINE, Prysm 0.9), a moving
layer (M.ENV 2 on M1PITCH), and the second oscillator. Captions only.

    tools/films/oscillators.py          -> takes/oscillators/oscillators.mp4, .srt, chapters.txt
    tools/films/oscillators.py 06-ring  film only that scene again (the others as filmed before), then join"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, film

NB = " "                                   # (a no-break space: "KNOB 1" stays on one line)
W = [0, 18, 36, 54, 73, 91, 109, 127]           # WAVE: the position of each of a bank's 8 waves
X05, X1, X15, X2, X3, X4, X7 = 0, 1, 2, 3, 4, 5, 8   # RATIO indices
FM, RING, MIX = 0, 1, 2
ST = lambda n: 24 + n                           # PIT / SEMI: semitones
CENTS = lambda c: 64 + round(c * 63 / 50)       # FINE


def scenes():
    S = []

    # 1 ---- the chain
    s = Scene("01-chain", "1 · Inside an oscillator", splash=True, tail=1)
    s.caption("Three waves in a chain",
              "Each of Prysm's two oscillators is a chain of three waves: mod 2 changes mod 1, mod 1 changes the "
              "carrier, and the carrier is what you hear. The picture shows the chain.")
    s.wait(400).on(48, 55, 64).wait(6000).off(48, 55, 64)
    s.caption("A clean start",
              "To hear an oscillator as it is, first open the filter all the way and take its envelope out (ENV to "
              "0), then turn oscillator 2 down on the MIX page.")
    s.go("FILTER").turn(1, 127, 1500).turn(3, 64, 1500)
    s.go("MIX").row("OSC").turn(2, 0, 1500)
    s.caption("One sine wave",
              f"Last, turn mod 1's amount down (OSC 1, MIX row, KNOB{NB}1). What is left is the carrier alone: a "
              "sine wave, the simplest sound there is.")
    s.go("OSC1").row("MIX").on(57).turn(1, 0, 3000).wait(3000).off(57)
    S.append(s)

    # 2 ---- waves
    s = Scene("02-waves", "2 · Waves and banks", after=S[-1])
    s.caption("The WAVE knob",
              "The carrier plays a wave from a bank of eight, and WAVE moves smoothly from one to the next. In BASIC: "
              "sine, rounded, triangle, soft saw, saw, square and two pulses.")
    s.row("WAVE").wait(300).on(45, 57).turn(2, 127, 9000).wait(1200).turn(2, W[4], 2000).wait(1500)
    s.caption("Six banks",
              "BANK picks the eight. HARM stacks organ-like harmonics, PULSE narrows a square, VOCAL sings vowels, "
              "DIGI is stepped and gritty, CMPLX folded and clustered.")
    for b in (1, 2, 3, 4, 5):
        s.turn(1, b, 400).turn(2, 0, 400).turn(2, 127, 3500)
    s.off(45, 57)
    s.caption("Back to a sine",
              "For FM it helps to start simple, so back to BASIC and the sine. Everything that follows builds on this "
              "one wave.")
    s.turn(1, 0, 600).turn(2, 0, 1500).on(57).wait(2500).off(57)
    S.append(s)

    # 3 ---- FM
    s = Scene("03-fm", "3 · FM and the ratio", after=S[-1])
    s.caption("Frequency modulation",
              "Mod 1 pushes the carrier's phase back and forth, fast. You do not hear a wobble: you hear new "
              "overtones. AMT1 is how hard it pushes.")
    s.row("MIX").on(57).turn(1, 90, 6000).wait(1000).turn(1, 50, 2500).wait(1000)
    s.caption("The ratio",
              "RATIO tunes mod 1 against the note. At x1 and x2 the overtones are harmonics, as in real "
              "instruments. Higher ratios reach higher: thinner, brassier.")
    s.row("RATIO").turn(2, X1, 300).wait(2500).turn(2, X2, 300).wait(2500).turn(2, X3, 300).wait(2500)
    s.turn(2, X4, 300).wait(2500)
    s.caption("Between the harmonics",
              "x1.5 and x0.5 put overtones between the harmonics, or an octave below: hollow, reedy tones. x7 rings "
              "like a bell.")
    s.turn(2, X15, 300).wait(2800).turn(2, X05, 300).wait(2800).turn(2, X7, 300).wait(2800).off(57)
    s.turn(2, X2, 300).wait(500)
    S.append(s)

    # 4 ---- the modulator's wave
    s = Scene("04-modwave", "4 · The modulator's wave", after=S[-1])
    s.caption("Mod 1 has a wave too",
              "The modulator is a wavetable operator like the carrier. Its wave changes the FM itself: a saw or a "
              "square pushes with all their overtones, and the result gets much richer.")
    s.row("WAVE").on(57).wait(1500).turn(4, W[4], 4000).wait(2500).turn(4, W[5], 1500).wait(2500)
    s.caption("Banks as modulators",
              "VOCAL as the modulator puts a vowel in the FM; DIGI gives grit. Small changes of WAVE now make big "
              "changes of tone, so turn slowly.")
    s.turn(3, 3, 400).turn(4, 0, 400).turn(4, 127, 7000).wait(1000)
    s.turn(3, 4, 400).turn(4, 40, 3000).wait(2000).off(57)
    s.caption("Tidy up",
              "Back to a sine modulator for the next part, so each change is easy to hear.")
    s.turn(3, 0, 500).turn(4, 0, 1200).on(57).wait(2500).off(57)
    S.append(s)

    # 5 ---- mod 2
    s = Scene("05-mod2", "5 · Mod 2: FM on the FM", after=S[-1])
    s.caption("The third wave",
              "Mod 2 modulates mod 1. Now the wave that shapes the carrier is itself FM: a second layer of "
              "overtones, from glassy to growling. AMT2 sets how much.")
    s.row("MIX").on(45, 57).turn(1, 60, 1000).turn(2, 80, 6000).wait(2500)
    s.caption("Mod 2's ratio",
              "Mod 2 has its own ratio. A low one roughens the tone; a high one adds a metallic edge on top.")
    s.row("RATIO").turn(3, X3, 300).wait(3000).turn(3, X7, 300).wait(3000).turn(3, X05, 300).wait(3000)
    s.off(45, 57)
    s.caption("Back to one modulator",
              "Mod 2 down again (AMT2 to 0), and its ratio back to x1.")
    s.turn(3, X1, 300).row("MIX").turn(2, 0, 2000).wait(800)
    S.append(s)

    # 6 ---- envelopes
    s = Scene("06-env", "6 · Modulator envelopes", after=S[-1])
    s.caption("Brightness over time",
              "Each modulator has an attack and a release (the ENV row). The amount rises over A1, holds while the key "
              "is down, and falls over R1 once you let go.")
    s.row("ENV").turn(1, 0, 400).turn(2, 30, 1200)
    s.play([57, 60, 64, 67], dur=600, gap=900).wait(500)
    s.caption("A swell",
              "A slow attack makes each note start soft and pure and grow brighter as you hold it, like a brass "
              "player blowing harder.")
    s.turn(1, 95, 2000).play([57, 64], dur=3800, gap=4300)
    s.caption("A pluck",
              "Turn the attack back to 0 and the release up: the brightness now dies away after the key is let go, a "
              "struck, bell-like tail.")
    s.turn(1, 0, 1200).turn(2, 105, 1500).play([57, 64, 69], dur=300, gap=1800).wait(1200)
    s.turn(2, 60, 1000)
    S.append(s)

    # 7 ---- RING
    s = Scene("07-ring", "7 · RING: pitch, not ratio", after=S[-1])
    s.caption("RING mode",
              "On the MODE row, mod 1 can multiply the carrier instead (RING). At a full amount you hear the sum and "
              "the difference of the two pitches, not the carrier itself.")
    s.row("MODE").turn(1, RING, 800).row("MIX").turn(1, 127, 1500).on(57).wait(3500)
    s.caption("RATIO becomes PIT1",
              "A modulator in RING or MIX is heard, so Prysm 0.9 tunes it in semitones. Back on the RATIO row, "
              "MOD1 is now PIT1: -24 to +24.")
    s.row("RATIO").wait(1500).turn(2, ST(7), 2500).wait(2500)
    s.caption("Metal and bells",
              "Odd intervals give clangorous, metallic tones: try a tritone (+6), a major third (+4), or +11. Each "
              "step is a new bell.")
    s.turn(2, ST(6), 600).wait(2500).turn(2, ST(4), 600).wait(2500).turn(2, ST(11), 600).wait(2500).off(57)
    s.play([57, 60, 62, 64, 67], dur=250, gap=600).wait(800)
    S.append(s)

    # 8 ---- MIX
    s = Scene("08-mix", "8 · MIX: layers", after=S[-1])
    s.caption("MIX mode",
              "MIX adds mod 1's wave to the carrier: a second voice inside the oscillator. AMT1 is its level, PIT1 "
              "its pitch. +12 is an octave up.")
    s.row("MODE").turn(1, MIX, 800).row("RATIO").turn(2, ST(12), 1200).on(45, 57).wait(4000)
    s.caption("Fifths and organs",
              "+7 adds a fifth, +19 a fifth an octave up: drawbar organ territory, from a single oscillator.")
    s.turn(2, ST(7), 800).wait(3000).turn(2, ST(19), 800).wait(3000).off(45, 57)
    s.caption("FINE1: beating",
              "FINE1 (MODE row) detunes the layer by up to 50 cents. A few cents and the two waves beat against each "
              "other: a slow chorus from one oscillator.")
    s.turn(2, ST(0), 800).row("MODE").on(45, 57).turn(3, CENTS(12), 4000).wait(4000).off(45, 57)
    S.append(s)

    # 9 ---- a moving layer
    s = Scene("09-move", "9 · Moving the layer", after=S[-1], tail=2)
    s.caption("M1PITCH",
              "The layer's pitch is a modulation destination too: M1PITCH. Here M.ENV 2 (ENV, four times) starts "
              "it an octave up on each note and lets it fall back.")
    s.go("MENV2").turn(2, 80, 1200).turn(3, 0, 600).row("DEST").turn(2, 16, 2500).turn(3, 127, 2000)
    s.play([57, 60, 64], dur=1800, gap=2200)
    s.caption("Looping it",
              "With LOOP on, the envelope runs over and over while the key is held: the layer sweeps down again and "
              "again, under a steady carrier.")
    s.row("ENV").turn(2, 55, 1200).row("DEST").turn(1, 1, 600).on(45, 57).wait(6000).off(45, 57)
    s.caption("Back to FM",
              "Loop off, amount to 0, and mod 1 back to FM (where its ratio is still x2: each mode keeps its own "
              "setting).")
    s.turn(1, 0, 600).turn(3, 64, 1500).go("OSC1").row("MODE").turn(1, FM, 800).turn(3, 64, 1000)
    s.row("MIX").turn(1, 60, 1200).on(57).wait(2500).off(57)
    S.append(s)

    # 10 ---- two oscillators
    s = Scene("10-two", "10 · Two oscillators", after=S[-1], tail=4)
    s.caption("Oscillator 2",
              "Oscillator 2 is a second chain of its own, with every control above (EDIT, twice). On the MIX page, "
              "OSC2 brings it in beside oscillator 1.")
    s.go("MIX").row("OSC").on(45, 57, 64).turn(2, 90, 3000).wait(2500)
    s.caption("SEMI and FINE",
              "SEMI moves oscillator 2 by semitones: -12 is a sub an octave down. FINE detunes it a little for the "
              "classic thick, moving two-oscillator sound.")
    s.turn(3, ST(-12), 1500).wait(2500).turn(4, CENTS(14), 2000).wait(3500).off(45, 57, 64)
    s.caption("Six waves a voice",
              "Two chains of three: six wavetable operators for every note, four notes at a time. The manual "
              "explains every knob: nicholaspratt.github.io/Prysm.")
    s.play([(45, 57, 64), (41, 57, 60), (43, 55, 62), (45, 57, 64)], dur=1700, gap=1900)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "oscillators", "PRYSM  ·  oscillators in depth", only=sys.argv[1:])
