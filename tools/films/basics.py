#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthesis basics with Prysm: the video manual, for players new to synthesizers. From the default sound (which a
viewer can load too), one idea at a time: oscillators, FM, the filter, envelopes, the LFO, effects, the arpeggiator
and sequencer, saving. Captions only (no voice-over), each up long enough to read.

    tools/films/basics.py               -> takes/basics/basics.mp4, basics.srt, chapters.txt (for YouTube)
    tools/films/basics.py 03-fm         film only that scene again (the others as filmed before), then join

Each scene carries on from the one before it (tools/film.py), so a change to one moves the start of the next: film
from the changed scene on (or all of it)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, film

NB = " "                                   # (a no-break space: "KNOB 1" stays on one line)


def scenes():
    S = []

    # 1 ---- what this is
    s = Scene("01-intro", "1 · Introduction", splash=True, tail=1)
    s.caption("Synthesis, one knob at a time",
              "A synthesizer builds sound from a few simple parts. This video takes Prysm's default sound apart, "
              "then builds new sounds from it, one idea at a time.")
    s.wait(400).on(48, 55, 64).wait(5000).off(48, 55, 64)
    s.caption("Play along",
              "Everything here is Prysm's real screen and sound. To follow on your FM-1, start from the same place: "
              "load the default sound, as shown.")
    s.press("SAVE", 900).patch_row("INIT").wait(400).press("SAVE", 1200).press("EDIT", 800)
    S.append(s)

    # 2 ---- oscillators
    s = Scene("02-osc", "2 · Oscillators", after=S[-1])
    s.caption("The oscillator",
              "Every sound starts with an oscillator: a wave that repeats hundreds of times a second. How fast it "
              "repeats is the note you hear. The shape of the wave is its tone.")
    s.wait(300).on(57).wait(3000)
    s.caption("Two oscillators",
              "Prysm has two oscillators, mixed together. The default sound uses both. Turn oscillator 2 down on the "
              "MIX page and you hear oscillator 1 alone.")
    s.go("MIX").row("OSC").turn(2, 0, 3500).wait(1500)
    s.caption("A pure sine wave",
              "Oscillator 1 also has some FM switched on (more about that soon). Turn it down too, and you hear a sine "
              "wave: the purest wave there is. Soft and round, like a flute.")
    s.go("OSC1").row("MIX").turn(1, 0, 3500).wait(2500)
    s.caption("Shape is tone",
              "Now turn the WAVE knob. The sine becomes a triangle, then a saw, then a square. Each shape adds "
              "overtones: higher frequencies that make a sound brighter or buzzier.")
    s.row("WAVE").turn(2, 127, 7000).wait(1500).turn(2, 0, 3000)
    s.caption("Wavetables",
              "Each bank holds 8 waves, and the WAVE knob morphs smoothly between them. There are six banks, from "
              "simple to complex. VOCAL sings vowels: oo, oh, ah, ee.")
    s.turn(1, 3, 600).turn(2, 127, 7000).wait(1000).turn(2, 0, 2000).turn(1, 0, 600)
    s.off(57)
    S.append(s)

    # 3 ---- FM
    s = Scene("03-fm", "3 · FM: brightness from a second wave", after=S[-1])
    s.caption("Frequency modulation",
              "FM is Prysm's main trick. A second wave, the modulator, pushes the main wave (the carrier) back and "
              "forth so fast that you hear new overtones instead of a wobble.")
    s.wait(300).on(57).wait(1500).row("MIX").turn(1, 95, 6000).wait(1500)
    s.caption("More FM, more brightness",
              "The more modulation, the brighter and more metallic the sound. On the screen, the arrow from the "
              "modulator to the carrier grows thicker as you turn it up.")
    s.turn(1, 30, 3000).wait(800).turn(1, 70, 3000).wait(1500)
    s.caption("The ratio sets the character",
              "RATIO tunes the modulator against the note. Whole numbers (x1, x2, x3) sound like instruments. In "
              "between, like x1.5, sounds hollow or like a bell.")
    s.row("RATIO").turn(2, 1, 300).wait(2200).turn(2, 4, 300).wait(2200).turn(2, 2, 300).wait(2200)
    s.turn(2, 3, 300).wait(1500)
    s.caption("Brightness that moves",
              "The modulator has its own envelope. Give it a slow attack and each note starts soft and grows brighter "
              "as you hold it, like a brass player blowing harder.")
    s.off(57).row("ENV").turn(1, 90, 2500)
    s.play([57, 57], dur=3200, gap=3600).turn(1, 0, 1500)
    S.append(s)

    # 4 ---- the filter
    s = Scene("04-filter", "4 · The filter", after=S[-1])
    s.caption("Taking sound away",
              "A filter removes overtones. A low-pass filter lets the lows through and cuts the highs. Turn its "
              "cutoff down and the sound gets darker, like a hand over your mouth.")
    s.go("FILTER")
    s.play([45, 45, 57, 45] * 9, dur=160, gap=330, at=s.t)
    s.wait(600).turn("cut", 30, 4500).wait(800).turn("cut", 110, 4000)
    s.caption("Resonance",
              "Resonance boosts the sound right at the cutoff. Turn it up, sweep the cutoff, and you get the classic "
              "synth \"wow\".")
    s.play([45, 45, 57, 45] * 9, dur=160, gap=330, at=s.t)
    s.turn(2, 105, 2000).turn("cut", 35, 4000).turn("cut", 95, 4000)
    s.caption("The filter envelope",
              "An envelope can move the cutoff on every note: up fast, then back down. Each note gets a bright "
              "\"pluck\" at its start. ENV sets how far it moves.")
    s.play([45, 45, 57, 45, 48, 45, 60, 45] * 5, dur=140, gap=300, at=s.t)
    s.turn("cut", 50, 1500).row("ENV").turn(2, 50, 1500).turn(3, 0, 1500).row("FILTER").turn(3, 120, 2500).wait(3000)
    s.caption("Steeper",
              "TYPE picks the filter. LP 24 cuts more steeply than LP 12: darker and smoother. (The comb types ring "
              "like strings; the manual has more.)")
    s.play([45, 45, 57, 45, 48, 45, 60, 45] * 4, dur=140, gap=300, at=s.t)
    s.row("TYPE").turn(1, 1, 300).wait(3500).row("FILTER").turn(2, 60, 1500).wait(3000)
    S.append(s)

    # 5 ---- envelopes
    s = Scene("05-env", "5 · Envelopes: shape over time", after=S[-1])
    s.caption("The amp envelope",
              "The amp envelope shapes each note's loudness: Attack (how it fades in), Decay (how it falls), Sustain "
              f"(the level while you hold the key) and Release (the fade{NB}out after).")
    s.go("AMP").wait(400).on(57).wait(3000).off(57).wait(2500)
    s.caption("A pluck",
              "Short attack, short decay, no sustain: each note dies away on its own, like a plucked string or a "
              "piano.")
    s.turn(3, 0, 1500).turn(2, 55, 1200)
    s.play([57, 60, 64, 69, 64, 60] * 2, dur=250, gap=420)
    s.caption("A pad",
              "Slow attack, full sustain, long release: the note swells in and lingers after you let go. That is a "
              "pad, a soft bed of sound.")
    s.turn(1, 95, 1500).turn(3, 127, 1500).turn(4, 100, 1500)
    s.on(48, 55, 63).wait(5000).off(48, 55, 63).wait(4000)
    S.append(s)

    # 6 ---- the LFO
    s = Scene("06-lfo", "6 · The LFO: movement", after=S[-1])
    s.caption("A slow oscillator",
              "An LFO (low-frequency oscillator) is too slow to hear as a note. Instead it moves another knob up and "
              "down, over and over: it brings a sound to life.")
    s.go("LFO").wait(300).on(48, 55, 63).wait(4000)
    s.caption("Vibrato",
              "Send the LFO to PITCH and the notes waver: vibrato, like a singer. RATE sets how fast it moves; the "
              "amount, how far.")
    s.row("DEST").turn(2, 70, 1500).wait(3500).row("LFO").turn(2, 70, 2000).wait(2500).row("DEST").turn(2, 64, 800)
    s.caption("A sweeping filter",
              "Send it to CUTOFF instead and the sound opens and closes by itself. Slow it right down for a long, "
              "breathing sweep.")
    s.turn(4, 110, 2000).row("LFO").turn(2, 40, 2500).wait(5000)
    s.caption("Morphing waves",
              "Or send it to WAVE1 and the oscillator moves through its wave bank as it plays: a sound that never "
              "stands still.")
    s.row("DEST").turn(3, 3, 1000).turn(4, 120, 1000).wait(6000)
    s.off(48, 55, 63)
    S.append(s)

    # 7 ---- effects
    s = Scene("07-fx", "7 · Effects", after=S[-1], tail=5)
    s.caption("Chorus",
              "Effects come after the sound. Chorus adds a slightly moving copy of it: wider, softer, shimmering.")
    s.go("CHO").wait(300).on(48, 55, 63).turn(3, 90, 2500).wait(4000).off(48, 55, 63).wait(1500)
    s.caption("Delay",
              "Delay plays echoes. TIME is the gap between them, FDBK (feedback) how many come back. With PING, they "
              "bounce from left to right.")
    s.press("FX").turn(3, 60, 1500)
    s.play([60, 67, 63, 70], dur=200, gap=700).wait(2500).turn(1, 80, 2000).play([60, 67, 63, 70], dur=200, gap=700)
    s.wait(2500)
    s.caption("Reverb",
              "Reverb puts the sound in a space. SIZE goes from a small room to a huge hall. A little goes a long way.")
    s.press("FX").turn(3, 70, 1500).turn(1, 115, 1500)
    s.on(48, 55, 63).wait(3000).off(48, 55, 63).wait(4500)
    S.append(s)

    # 8 ---- patterns
    s = Scene("08-arp-seq", "8 · Arpeggiator and sequencer", after=S[-1], tail=3)
    s.caption("Back to a pluck",
              "Patterns sound best with short notes, so first: a short attack and a low sustain on the AMP page, and "
              "the effects a little lower.")
    s.go("AMP").turn(1, 5, 1500).turn(3, 20, 1500).turn(4, 50, 1200)
    s.go("DLY").turn(3, 30, 1200).press("FX").turn(3, 35, 1200)
    s.caption("The arpeggiator",
              "Hold a chord, and the arpeggiator plays its notes one at a time, in time. MODE sets the order: up, "
              "down, up and down, random.")
    s.go("ARP").press("ARP", 300).on(57, 60, 64).wait(4000)
    s.turn(2, 2, 400).wait(3500)
    s.caption("More octaves",
              "OCT spreads the pattern over more octaves, and BPM on the TIME row sets the tempo. Let go of the keys "
              "and it stops.")
    s.turn(4, 1, 400).wait(4500).off(57, 60, 64).press("ARP", 600)
    s.caption("The sequencer",
              f"The sequencer plays a pattern of up to 64 steps. Press REC, then play keys: each key fills the next "
              f"step.")
    s.press("REC", 700)
    for n in [45, 57, 45, 48, 45, 55, 45, 60]:
        s.play([n], dur=200, gap=450)
    s.press("REC", 600)
    s.caption("Play it",
              "PLAY runs the pattern (here 8 notes, then 8 rests). Turn the cutoff while it plays: it is a "
              "performance.")
    s.press("PLAY", 3000).turn("cut", 30, 3500).turn("cut", 90, 3500).wait(1500).press("PLAY", 500)
    S.append(s)

    # 9 ---- saving
    s = Scene("09-save", "9 · Saving your sound", after=S[-1], tail=4)
    s.caption("Keep it",
              f"Made something you like? Press SAVE, choose a slot with KNOB{NB}1, and press SAVE again. The FM-1 "
              "remembers it, even when switched off.")
    s.press("SAVE", 1000).patch_row("SAVE").slot(2).wait(600).press("SAVE", 2500)
    s.caption("Keep exploring",
              "Every page draws what it does, so try things and watch. The manual explains every knob: "
              "nicholaspratt.github.io/Prysm. Have fun!")
    s.press("EDIT", 500).on(48, 55, 64).wait(4000).off(48, 55, 64)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "basics", "PRYSM  ·  synthesis basics", only=sys.argv[1:])
