#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The comb filter in a minute: from the default sound to a plucked string, then bells and metal. Captions only.

    tools/films/comb.py                 -> takes/comb/comb.mp4, comb.srt, chapters.txt"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from film import Scene, film

NB = " "
RIFF = [48, 55, 60, 64, 67, 64, 60, 55]


def scenes():
    S = []
    s = Scene("01-comb", "The comb filter", tail=1)
    s.caption("A filter that rings",
              "Prysm's filter has a second life. As a comb, it rings at a note's harmonics, like a string. From the "
              "default sound: on the FILTER page, set TYPE to COMB+.")
    s.go("FILTER").row("TYPE")
    s.play(RIFF * 2, dur=180, gap=380, at=s.t)
    s.wait(1500).turn(1, 2, 300).wait(2500)
    s.caption("Tune it to the note",
              f"CUT at 64 and KEY at 127 put the comb's peaks on each note's own harmonics. ENV at 0 keeps its pitch "
              f"still. RES is the feedback: how long it rings.")
    s.play(RIFF * 3, dur=180, gap=380, at=s.t)
    s.row("FILTER").turn(1, 64, 1500).turn(4, 127, 1500).turn(3, 64, 1200).turn(2, 115, 2000).wait(1500)
    S.append(s)

    s = Scene("02-pluck", "The comb filter", after=S[-1])
    s.caption("A plucked string",
              "Let the comb do the ringing: on the AMP page, no sustain and a long release. Each note is now a short "
              "burst that the comb turns into a string.")
    s.go("AMP").turn(3, 0, 1200).turn(2, 85, 1200).turn(4, 100, 1200)
    s.play(RIFF * 2, dur=180, gap=420)
    s.caption("Strings, bells, metal",
              f"On the COMB row, SPREAD stretches the upper peaks apart: from a string to a bell to a sheet of metal. "
              f"DAMP darkens the ringing.")
    s.go("FILTER").row("COMB")
    s.play(RIFF * 4, dur=180, gap=420, at=s.t)
    s.wait(800).turn(2, 110, 6000).wait(1500).turn(1, 90, 2500).wait(2500)
    s.caption("COMB-",
              "COMB- puts the peaks halfway between: hollow, and an octave down. Try both on any sound. The manual "
              "has more: nicholaspratt.github.io/Prysm")
    s.turn(2, 0, 1500).turn(1, 40, 800).row("TYPE")
    s.play(RIFF * 2, dur=180, gap=420, at=s.t)
    s.wait(1500).turn(1, 3, 300).wait(4000)
    S.append(s)
    return S


if __name__ == "__main__":
    film(scenes(), "comb", "PRYSM  ·  the comb filter", only=sys.argv[1:])
