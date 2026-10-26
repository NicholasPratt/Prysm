#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What's new in Prysm 1.2, in about four minutes: the noise oscillator (its six types), filter routing (DIRECT past
the filter), the lo-fi drum machine (a beat on the keys, the kit, CRUSH, the drums through the filter). From the
default sound. Captions only.

    tools/films/whatsnew12.py           -> takes/whatsnew12/whatsnew12.mp4, .srt, chapters.txt
    tools/films/whatsnew12.py 04-drums  film only that scene again (the others as filmed before), then join"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, film

NB = " "
STEP = 125                                      # a 1/16 at the default 120 BPM, ms
BAR = 16 * STEP
WHITE, PINK, BROWN, DIGI, CRACKLE, SH = range(6)   # the noise's TYPE
STEP_KEY = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]   # step n's white key (DRUM page)
KICK_KEY, SNARE_KEY, HAT_KEY = 1, 3, 5          # F#3, G#3, A#3
CHORDS = [(45, 52, 60), (41, 53, 57), (43, 50, 59), (48, 55, 64)]


def steps(*ns):                                 # the keys of steps ns (1 .. 16)
    return [STEP_KEY[n - 1] for n in ns]


def scenes():
    S = []

    # 1 ---- intro
    s = Scene("01-intro", "1 · Prysm 1.2", splash=True, tail=1)
    s.caption("What's new in 1.2",
              "A noise oscillator with six flavours, a choice of what goes through the filter, and a crunchy "
              "three-drum machine with a lane in every pattern.")
    s.wait(400).on(48, 55, 64).wait(5500).off(48, 55, 64)
    S.append(s)

    # 2 ---- noise
    s = Scene("02-noise", "2 · The noise oscillator", after=S[-1], tail=2)
    s.caption("A third source",
              "EDIT now steps OSC 1, OSC 2, then NOISE. Its LEVEL mixes noise in with the two oscillators: here, "
              "breath on a chord.")
    s.go("NOISE").on(48, 55, 64).turn(3, 70, 3000).wait(2500)
    s.caption("White, pink, brown",
              "TYPE picks the noise. PINK and BROWN are darker, with more low end. COLOUR is a tone control: turn it "
              "down to soften the hiss.")
    s.turn(1, PINK, 600).wait(2200).turn(1, BROWN, 600).wait(2200).turn(1, WHITE, 600)
    s.turn(2, 30, 2500).wait(1000).turn(2, 100, 1500).off(48, 55, 64)
    s.caption("DIGI plays the note",
              "DIGI is a buzzy, metallic noise, like an old games console. It follows the keys; its COLOUR knob "
              "becomes RATE, in semitones.")
    s.turn(1, DIGI, 600).turn(2, 64, 800).turn(3, 110, 600)
    s.play([57, 60, 64, 62, 60, 57, 55, 57], dur=300, gap=340)
    s.on(57).turn(2, 88, 2000).wait(600).turn(2, 64, 1200).off(57)
    s.caption("Crackle and S&H",
              "CRACKLE is vinyl clicks; COLOUR becomes DENSE, how many. S&H is stepped, crunchy hiss that follows "
              "the keys, like a sample-and-hold.")
    s.turn(1, CRACKLE, 600).turn(2, 40, 600).on(48, 55, 64).turn(2, 90, 3000).wait(1000).off(48, 55, 64)
    s.turn(1, SH, 600).turn(2, 64, 600).play([45, 48, 52, 57, 52, 48], dur=280, gap=320)
    s.caption("The picture",
              "The page shows the noise's wave, and its spectrum: the bars move as TYPE and COLOUR change it.")
    s.turn(1, WHITE, 600).turn(2, 100, 600).on(48, 55, 64).turn(2, 40, 2500).turn(2, 100, 1500)
    s.turn(3, 0, 1500).off(48, 55, 64)
    S.append(s)

    # 3 ---- routing
    s = Scene("03-route", "3 · Filter routing", after=S[-1], tail=2.5)
    s.caption("A dark filter",
              "Close the filter (PRESETS turns CUTOFF from any page) and the whole sound goes dark: both "
              "oscillators go through it.")
    s.go("MIX").row("ROUTE").on(45, 57).turn("cut", 35, 3000).wait(1500)
    s.caption("DIRECT",
              "The MIX page's new ROUTE row sets OSC1, OSC2 and NOISE each to FILTER or DIRECT. OSC2 DIRECT skips "
              "the filter: bright over dark.")
    s.turn(2, 1, 600).wait(3000).turn(2, 0, 600).wait(1500).turn(2, 1, 600).wait(2000).off(45, 57)
    s.caption("Still enveloped",
              "A DIRECT source still goes through the amp envelope and the effects. Only the filter is skipped, so "
              "it plays like the rest of the sound.")
    s.play([(45, 57), (48, 60), (43, 55), (41, 53)], dur=900, gap=1100)
    s.turn(2, 0, 600).turn("cut", 92, 1500)
    S.append(s)

    # 4 ---- drums (one scene: the patterns do not carry from one scene to the next)
    s = Scene("04-drums", "4 · The drum machine", after=S[-1], tail=3)
    s.caption("The DRUM page",
              "Press SEQ twice. KICK, SNARE and HAT: 8-bit samples at a low rate, crunchy by design. Each pattern "
              "has a drum lane, under its notes.")
    s.go("SEQ").press("SEQ", 1200).wait(2500)
    s.caption("Keys are steps",
              "Here the 16 white keys are the 16 steps, as on a TR. Press one for a hit; the key's LED lights. The "
              "kick, on 1, 9 and 11.")
    s.key(*steps(1, 9, 11), gap=500)
    s.caption("Pick a drum",
              "The three lowest black keys pick KICK, SNARE or HAT, and play it. The snare goes on 5 and 13.")
    s.key(SNARE_KEY, gap=800, label="press G#3 (SNARE)").key(*steps(5, 13), gap=600)
    s.caption("Accents",
              "Press a step twice for an accent: louder. On the hat, an accent plays the open hat, cut off by the "
              "next hat, as on an 808.")
    s.key(HAT_KEY, gap=800, label="press A#3 (HAT)").key(*steps(1, 3, 5, 7, 9, 11, 13, 15), gap=320)
    s.key(*steps(15), gap=600)
    s.caption("Play it",
              "PLAY runs the pattern, drums and notes together, in the song too. The marker shows the step playing.")
    s.press("PLAY", 0)
    s.play([CHORDS[i % 4] for i in range(2)], dur=2 * BAR - 150, gap=2 * BAR, at=s.t)
    s.wait(4 * BAR)
    s.caption("Tune, decay",
              "Each drum has a row: LEVEL, TUNE (24 semitones either way) and DECAY. A shorter kick, tuned down, "
              "for a tighter thump.")
    s.play([CHORDS[(i + 2) % 4] for i in range(2)], dur=2 * BAR - 150, gap=2 * BAR, at=s.t)
    s.row("KICK").twist(2, -5, "TUNE", 1200).twist(3, -60, "DECAY", 3000).wait(2 * BAR)
    s.caption("Crunch",
              "The BUS row is the whole kit's: DRIVE saturates it, CRUSH takes away bits and sample rate. From a "
              "little grit to a broken toy.")
    s.play([CHORDS[i % 4] for i in range(3)], dur=2 * BAR - 150, gap=2 * BAR, at=s.t)
    s.row("BUS").twist(1, 70, "DRIVE", 2500).twist(2, 90, "CRUSH", 4000).wait(BAR).twist(2, -70, "CRUSH", 2000)
    s.wait(BAR)
    s.caption("Through the filter",
              "FILTER on each drum's row sends it through a filter that follows the sound's CUTOFF and RES. With "
              "snare and hat sent, the cutoff sweeps them with the synth.")
    s.row("SNARE").twist(4, 127, "FILTER", 1500).row("HAT").twist(4, 127, "FILTER", 1500)
    s.on(*CHORDS[3]).turn("cut", 30, 4000).wait(BAR).turn("cut", 100, 5000)
    s.off(*CHORDS[3])
    s.caption("The kick stays full",
              "The kick's FILTER is still 0, so it stays dry: the cutoff sweeps everything else, a classic build. "
              "The kit is saved with the patterns, not the sound.")
    s.play([CHORDS[(i + 1) % 4] for i in range(2)], dur=2 * BAR - 150, gap=2 * BAR, at=s.t)
    s.turn("cut", 40, 3000).turn("cut", 92, 3000).wait(BAR)
    s.press("PLAY", 600)
    S.append(s)

    # 5 ---- get it
    s = Scene("05-get", "5 · Get it", after=S[-1], tail=4)
    s.press("HOME", 0)                          # (the scope: the patterns do not carry over, an empty DRUM page would show)
    s.caption("MIDI channel 10",
              "A DAW can play the drums: notes on channel 10, 36 kick, 38 snare, 42 closed hat, 46 open hat. "
              "The CLAP plugin plays them too.")
    for i in range(8):                          # (a beat from MIDI: kick, hat, snare, hat)
        n = [36, 42, 38, 42][i % 4]
        s.ev.append((s.t + i * 250, f"midi 0x99 {n} 110"))
        if i % 4 == 1: s.ev.append((s.t + i * 250, "midi 0x99 36 90"))
    s.on(48, 55, 64).wait(2000).off(48, 55, 64).wait(2000)
    s.caption("Install 1.2",
              "Free, from the web installer at nicholaspratt.github.io/Prysm (Chrome or Edge). Your saved sounds, "
              "patterns and song come along.")
    s.play([(45, 52, 60), (41, 53, 57), (43, 50, 59), (48, 55, 64)], dur=1200, gap=1400)
    s.caption("The editor and the plugin",
              "The browser editor and the CLAP plugin know everything new: the NOISE page, the routing, and a DRUM "
              "tab with the step grid and the kit.")
    s.play([(45, 52, 60), (41, 53, 57), (43, 50, 59), (48, 55, 64)], dur=1200, gap=1400)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "whatsnew12", "PRYSM  ·  what's new in 1.2", only=sys.argv[1:], ver="1.2")
