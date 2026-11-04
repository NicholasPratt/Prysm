#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate firmware/gen/prysm_trans.h: the attack transients (the TRANS page), short sounds played at the start of
each note on top of an oscillator, as the D-50 put a sampled attack in front of a synthesised body. Every one is made
here from scratch (physical and spectral models, no recordings).

  tools/gen_transients.py               (needs numpy; the header is checked in, rerun after a change here)
  tools/gen_transients.py --wav DIR     also writes each as a WAV (as stored), to listen to

Stored in 8-bit mu-law (mu 255: ~13 bits' worth of quiet detail, a 256-entry table to decode), each at its own rate
(16 kHz for the dark ones, 22.05 kHz, 32 kHz for clicks and glass). The pitched ones are made at C4 (261.63 Hz) and
follow the key; the others (TRACK 0) play at their own pitch on every note. TUNE moves either.
"""
import argparse
import wave
from pathlib import Path

import numpy as np

FS = 44100
F0 = 261.6256                                   # C4: a pitched transient's pitch at MIDI note 60
OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "prysm_trans.h"
rng = np.random.default_rng(50)
MU = 255.0


def t_of(sec):
    return np.arange(int(sec * FS)) / FS


def noise(n):
    return rng.uniform(-1, 1, n)


def biquad(x, kind, fc, q):
    """RBJ biquad: 'lp', 'hp', 'bp' (constant peak gain)"""
    w = 2 * np.pi * fc / FS
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    if kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
    elif kind == "hp":
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    else:
        b = [al, 0, -al]
    a = [1 + al, -2 * c, 1 - al]
    b = [v / a[0] for v in b]
    a1, a2 = a[1] / a[0], a[2] / a[0]
    y = np.empty_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b[0] * v + b[1] * x1 + b[2] * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def dec(t, tau):
    return np.exp(-t / tau)


def att(t, a):                                  # a fast linear rise over a seconds (no click at the start)
    return np.clip(t / a, 0, 1) if a > 0 else np.ones_like(t)


def partials(t, ratios, decays, amps, f=F0):
    x = np.zeros_like(t)
    for r, d, a in zip(ratios, decays, amps):
        x += a * np.sin(2 * np.pi * f * r * t + rng.uniform(0, 2 * np.pi)) * dec(t, d)
    return x


def ks(sec, f, bright, damp):
    """Karplus-Strong: a noise burst, low-passed by bright (0..1), round a loop of one period, averaged by damp"""
    n = int(sec * FS)
    p = FS / f
    pi = int(p)
    fr = p - pi
    buf = biquad(noise(pi + 2), "lp", 800 + bright * 9000, 0.7)
    y = np.zeros(n)
    line = list(buf)
    for i in range(n):
        a = line[-pi]
        b = line[-pi - 1] if pi + 1 <= len(line) else a
        v = (1 - fr) * a + fr * b                # (the fraction of a period: in tune)
        v = damp * v + (1 - damp) * line[-1]
        y[i] = v
        line.append(v)
    return y


def saw(t, f):
    ph = np.cumsum(np.broadcast_to(f, t.shape) / FS)
    return 2 * (ph % 1.0) - 1


# ---- the sounds: (name, track, rate, x)
def hammer():
    t = t_of(0.18)
    felt = biquad(noise(len(t)), "lp", 1400, 0.7) * dec(t, 0.006)
    body = partials(t, [1, 2, 3, 4.02], [0.07, 0.04, 0.025, 0.015], [1, 0.6, 0.35, 0.2])
    thump = np.sin(2 * np.pi * 85 * t) * dec(t, 0.025)
    return (0.9 * felt + 0.6 * body + 0.7 * thump) * att(t, 0.0008)


def pluck():
    t = t_of(0.2)
    return ks(0.2, F0, 0.8, 0.5) * dec(t, 0.12)


def pick():
    t = t_of(0.12)
    click = biquad(noise(len(t)), "hp", 3000, 0.7) * dec(t, 0.0015)
    return 0.8 * click + ks(0.12, F0, 1.0, 0.45) * dec(t, 0.05)


def mallet():
    t = t_of(0.2)
    knock = biquad(noise(len(t)), "bp", 2500, 2) * dec(t, 0.003)
    return partials(t, [1, 3.93, 9.2], [0.12, 0.03, 0.01], [1, 0.5, 0.3]) * att(t, 0.0005) + 0.5 * knock


def kalimba():
    t = t_of(0.25)
    return partials(t, [1, 5.95, 13.2], [0.2, 0.025, 0.008], [1, 0.45, 0.25]) * att(t, 0.0005)


def bell():
    t = t_of(0.3)
    r = [0.5, 1, 1.19, 1.56, 2, 2.51, 2.66, 3.01, 4.1]
    d = [0.25, 0.2, 0.12, 0.1, 0.08, 0.05, 0.05, 0.04, 0.03]
    a = [0.5, 1, 0.7, 0.6, 0.5, 0.35, 0.3, 0.25, 0.2]
    return partials(t, r, d, a) * att(t, 0.0003)


def glass():
    t = t_of(0.2)
    return partials(t, [2.0, 5.43, 8.91, 13.1], [0.1, 0.04, 0.025, 0.015], [1, 0.6, 0.4, 0.25]) * att(t, 0.0002)


def metal():
    t = t_of(0.22)
    idx = 7 * dec(t, 0.03)
    return np.sin(2 * np.pi * F0 * t + idx * np.sin(2 * np.pi * F0 * 1.414 * t)) * dec(t, 0.08) * att(t, 0.0002)


def anvil():
    t = t_of(0.25)
    fs = rng.uniform(900, 5200, 9)
    x = sum(np.sin(2 * np.pi * f * t) * dec(t, rng.uniform(0.03, 0.12)) for f in fs) / 4
    hit = biquad(noise(len(t)), "hp", 2000, 0.7) * dec(t, 0.004)
    return (x + hit) * att(t, 0.0002)


def chiff():
    t = t_of(0.09)
    return biquad(noise(len(t)), "bp", 3 * F0, 5) * att(t, 0.004) * dec(t, 0.025) * 3


def breath():
    t = t_of(0.25)
    x = biquad(noise(len(t)), "bp", 1300, 0.8) + 0.4 * biquad(noise(len(t)), "bp", 3500, 1.5)
    return x * att(t, 0.06) * np.where(t < 0.06, 1, dec(t - 0.06, 0.07))


def flute():
    t = t_of(0.2)
    air = biquad(noise(len(t)), "bp", 2 * F0, 3) * 2.5
    tone = np.sin(2 * np.pi * F0 * t) * att(t, 0.04) * 0.6
    return (air * att(t, 0.01) * dec(t, 0.05) + tone * dec(t, 0.09))


def bow():
    t = t_of(0.2)
    jit = 1 + 0.02 * biquad(noise(len(t)), "lp", 60, 0.7) * 8
    x = saw(t, F0 * jit)
    rosin = biquad(noise(len(t)), "bp", 2500, 1) * 0.5
    return biquad(x + rosin, "lp", 3000, 0.7) * att(t, 0.02) * dec(t, 0.08)


def brass():
    t = t_of(0.15)
    f = F0 * 2 ** (-1.0 / 12 * dec(t, 0.02))      # a scoop up from a semitone under
    x = saw(t, f)
    y = np.zeros_like(x)
    s = 0.0
    fc = 300 + 4000 * (1 - dec(t, 0.015)) * dec(t, 0.06)   # the lips: open fast, then back
    for i, v in enumerate(x):
        s += (1 - np.exp(-2 * np.pi * fc[i] / FS)) * (v - s)
        y[i] = s
    return y * att(t, 0.003) * dec(t, 0.06) * 1.6


def slap():
    t = t_of(0.12)
    f = F0 / 2 * (1 + 0.6 * dec(t, 0.008))
    pop = np.sin(2 * np.pi * np.cumsum(f) / FS) * dec(t, 0.05)
    click = biquad(noise(len(t)), "hp", 2500, 0.7) * dec(t, 0.002)
    buzz = biquad(noise(len(t)), "bp", 1800, 2) * dec(t, 0.015) * 0.6
    return pop + 0.7 * click + buzz


def knock():
    t = t_of(0.1)
    imp = np.zeros_like(t)
    imp[0:3] = [1, -0.6, 0.3]
    return (biquad(imp, "bp", 900, 25) * 6 + biquad(imp, "bp", 1650, 30) * 4) * dec(t, 0.04)


def click():
    t = t_of(0.02)
    return biquad(noise(len(t)), "hp", 1500, 0.7) * dec(t, 0.0012)


def tick():
    t = t_of(0.03)
    return biquad(noise(len(t)), "bp", 6500, 3) * dec(t, 0.004) * 2


def thump():
    t = t_of(0.12)
    f = 45 + 50 * dec(t, 0.02)
    return np.sin(2 * np.pi * np.cumsum(f) / FS) * dec(t, 0.06) * att(t, 0.001)


def zap():
    t = t_of(0.12)
    f = 200 + 3800 * dec(t, 0.012)
    return np.sin(2 * np.pi * np.cumsum(f) / FS) * dec(t, 0.05) * att(t, 0.0005)


def blip():
    t = t_of(0.06)
    f = np.where(t < 0.012, 4 * F0, 2 * F0)
    return np.sign(np.sin(2 * np.pi * np.cumsum(f) / FS)) * dec(t, 0.02) * att(t, 0.0005) * 0.6


def burst():
    t = t_of(0.1)
    return noise(len(t)) * dec(t, 0.025) * att(t, 0.0005)


def spit():
    t = t_of(0.08)
    return biquad(noise(len(t)), "hp", 4500, 0.7) * att(t, 0.001) * dec(t, 0.02) * 1.5


def vox():
    t = t_of(0.2)
    ph = (t * F0) % 1.0                             # a glottal pulse: a rise, a fast fall
    g = np.where(ph < 0.6, np.sin(np.pi * ph / 0.6 / 2) ** 2, np.cos(np.pi * (ph - 0.6) / 0.4 / 2) ** 2) - 0.4
    x = biquad(g, "bp", 700, 6) + 0.6 * biquad(g, "bp", 1220, 8) + 0.3 * biquad(g, "bp", 2600, 10)
    return x * att(t, 0.01) * dec(t, 0.07)


SOUNDS = [                                      # name (6 letters at most: the knob's value), track, rate
    ("HAMMER", 1, 16000, hammer), ("PLUCK", 1, 22050, pluck), ("PICK", 1, 22050, pick),
    ("MALLET", 1, 16000, mallet), ("KALIMB", 1, 22050, kalimba), ("BELL", 1, 22050, bell),
    ("GLASS", 1, 32000, glass), ("METAL", 1, 22050, metal), ("ANVIL", 0, 22050, anvil),
    ("CHIFF", 1, 22050, chiff), ("BREATH", 0, 16000, breath), ("FLUTE", 1, 16000, flute),
    ("BOW", 1, 16000, bow), ("BRASS", 1, 22050, brass), ("SLAP", 1, 22050, slap),
    ("KNOCK", 0, 16000, knock), ("CLICK", 0, 32000, click), ("TICK", 0, 32000, tick),
    ("THUMP", 0, 16000, thump), ("ZAP", 0, 22050, zap), ("BLIP", 1, 22050, blip),
    ("NOISE", 0, 22050, burst), ("SPIT", 0, 32000, spit), ("VOX", 1, 16000, vox),
]


def resample(x, rate):
    """to rate: everything above 0.45 of it removed (an FFT brick wall), then read at the new spacing"""
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / FS)
    X[f > 0.45 * rate] = 0
    y = np.fft.irfft(X, n)
    pos = np.arange(0, n - 1, FS / rate)
    return np.interp(pos, np.arange(n), y)


def mulaw(x):
    y = np.sign(x) * np.log1p(MU * np.abs(x)) / np.log1p(MU)
    return np.clip(np.round(y * 127), -127, 127).astype(int)


def unmulaw(q):
    y = np.clip(q, -127, 127) / 127.0             # (-128: never written; read as -127)
    return np.sign(y) * ((1 + MU) ** np.abs(y) - 1) / MU


def make():
    out = []
    for name, track, rate, fn in SOUNDS:
        x = fn()
        x = resample(x, rate)
        x = x / (np.max(np.abs(x)) + 1e-9) * 0.97
        fade = min(len(x), int(0.004 * rate))           # the last 4 ms to silence: no click at its end
        x[-fade:] *= np.linspace(1, 0, fade)
        out.append((name, track, rate, mulaw(x)))
    return out


def c_rows(vals, per=24):
    s = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return ",\n    ".join(s)


def write(sounds):
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* Generated by tools/gen_transients.py: do not edit. The attack transients (the TRANS page): 8-bit mu-law,",
             " * each at its own rate; the pitched ones (track 1) made at C4 */",
             "#ifndef PRYSM_TRANS_H", "#define PRYSM_TRANS_H", "#include <stdint.h>", ""]
    table = [int(round(v * 32767)) for v in unmulaw(np.arange(256).astype(np.int8).astype(float))]
    lines.append("static const int16_t TR_MU[256] = {   /* a byte (as int8, its index as uint8) -> Q15 */")
    lines.append("    " + c_rows(table, 16) + "};")
    total = 0
    for i, (name, track, rate, q) in enumerate(sounds):
        lines.append(f"static const int8_t TR_D{i}[{len(q)}] = {{   /* {name}: {len(q) * 1000 // rate} ms at {rate} Hz */")
        lines.append("    " + c_rows(list(q)) + "};")
        total += len(q)
    lines.append(f"#define TR_N {len(sounds)}u                          /* ({total} bytes in all) */")
    lines.append("static const char *const TR_NAME[TR_N + 1u] = {\"OFF\", " +
                 ", ".join(f'"{n}"' for n, *_ in sounds) + "};")
    lines.append("typedef struct { const int8_t *d; uint16_t n; uint8_t track; uint32_t inc; } tr_sound_t;   "
                 "/* inc: its rate / 44100, Q16 */")
    lines.append("static const tr_sound_t TR_SOUND[TR_N] = {")
    for i, (name, track, rate, q) in enumerate(sounds):
        lines.append(f"    {{TR_D{i}, {len(q)}, {track}, {round(rate / FS * 65536)}u}},   /* {name} */")
    lines.append("};")
    lines.append("#endif")
    OUT.write_text("\n".join(lines) + "\n")
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", type=Path)
    a = ap.parse_args()
    sounds = make()
    total = write(sounds)
    print(f"{OUT}: {len(sounds)} transients, {total} bytes")
    if a.wav:
        a.wav.mkdir(parents=True, exist_ok=True)
        for name, track, rate, q in sounds:
            y = (unmulaw(q.astype(float)) * 32767).astype("<i2")
            with wave.open(str(a.wav / f"{name.lower()}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(y.tobytes())


if __name__ == "__main__":
    main()
