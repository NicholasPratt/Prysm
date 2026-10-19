#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the engine's test renders (tests/engine_test.c -> build/engine/*.wav) by their spectra and envelopes.

  python3 tests/check_engine.py      (run by tests/run_engine.sh; needs numpy)
"""
import sys
import wave
from pathlib import Path

import numpy as np

D = Path(__file__).resolve().parents[1] / "build" / "engine"
FS = 44100
fails = []


def load(name):
    with wave.open(str(D / f"{name}.wav")) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2)
    return x[:, 0].astype(float) / 32768


def spectrum(x, t0=0.2, t1=0.8):
    seg = x[int(t0 * FS):int(t1 * FS)]
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    return np.fft.rfftfreq(len(seg), 1 / FS), s / (s.max() or 1)


def level_db(f, s, hz, width=6.0):
    """the strongest bin within `width` Hz of hz, dB relative to the strongest bin overall"""
    m = np.abs(f - hz) <= width
    return 20 * np.log10(s[m].max() + 1e-12)


def peak_hz(f, s):
    i = int(s.argmax())
    a, b, c = s[i - 1], s[i], s[i + 1]          # parabolic interpolation
    return f[i] + (0.5 * (a - c) / (a - 2 * b + c)) * (f[1] - f[0])


def check(name, ok, detail):
    print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")
    if not ok:
        fails.append(name)


def rms(x, t0, t1):
    return float(np.sqrt(np.mean(x[int(t0 * FS):int(t1 * FS)] ** 2)))


print("engine checks")

f, s = spectrum(load("sine"))
hz = peak_hz(f, s)
worst = max(level_db(f, s, 440 * h) for h in range(2, 8))
check("sine pitch", abs(hz - 440) < 0.5, f"{hz:.2f} Hz")
check("sine purity", worst < -50, f"strongest harmonic {worst:.1f} dB")

f, s = spectrum(load("fm"))
h = [level_db(f, s, 440 * k) for k in range(1, 6)]
off = level_db(f, s, 660, 30)                   # between harmonics: nothing
check("fm harmonics", h[1] > -30 and h[2] > -40, "x2 {:.1f} dB, x3 {:.1f} dB".format(h[1], h[2]))
check("fm harmonic only", off < -60, f"between harmonics {off:.1f} dB")

f, s = spectrum(load("ring"))
a, b, c = level_db(f, s, 880), level_db(f, s, 1760), level_db(f, s, 1320)
check("ring sidebands", a > -3 and b > -3, f"880 Hz {a:.1f} dB, 1760 Hz {b:.1f} dB")
check("ring no carrier", c < -40, f"1320 Hz {c:.1f} dB")

f, s = spectrum(load("mix"))
a, b = level_db(f, s, 440), level_db(f, s, 880)
check("mix both", a > -3 and b > -3, f"440 Hz {a:.1f} dB, 880 Hz {b:.1f} dB")

f, s = spectrum(load("chain"))
fm1 = spectrum(load("fm"))[1]
n_chain = sum(level_db(f, s, 440 * k) > -40 for k in range(1, 20))
n_fm = sum(level_db(f, fm1, 440 * k) > -40 for k in range(1, 20))
off = max(level_db(f, s, 440 * k + 220, 30) for k in range(1, 8))
check("chain richer", n_chain > n_fm, f"{n_chain} harmonics above -40 dB (fm alone: {n_fm})")
check("chain harmonic only", off < -60, f"between harmonics {off:.1f} dB")

x = load("modenv")                              # brightness: the share of energy above 600 Hz, early and late


def bright(t0, t1):
    f, s = spectrum(x, t0, t1)
    return float((s[f > 600] ** 2).sum() / (s ** 2).sum())


early, late = bright(0.0, 0.05), bright(0.6, 0.85)
check("mod envelope", early < 0.2 * late, f"bright share {early:.3f} at 0-50 ms, {late:.3f} at 600-850 ms")

x = load("adsr")
env = np.array([rms(x, t, t + 0.01) for t in np.arange(0, 1.99, 0.01)])
t_peak = float(np.argmax(env)) * 0.01
sus = rms(x, 0.8, 0.95) / env.max()
rel = rms(x, 1.6, 1.7) / env.max()
check("adsr attack", 0.12 < t_peak < 0.3, f"peak at {t_peak * 1000:.0f} ms")
check("adsr sustain", 0.4 < sus < 0.6, f"sustain {sus:.2f} of peak (want 0.5)")
check("adsr release", rel < 0.1, f"{rel:.3f} of peak, 600 ms after the release")

f, s = spectrum(load("osc2"))
a, b = level_db(f, s, 440), level_db(f, s, 659.26)
check("osc2 a fifth", a > -3 and b > -3, f"440 Hz {a:.1f} dB, 659 Hz {b:.1f} dB")

x = load("chord")
pk = np.abs(x).max()
check("chord headroom", pk < 0.999, f"peak {pk:.3f} of full scale")

print("all passed" if not fails else f"{len(fails)} failed: {', '.join(fails)}")
sys.exit(1 if fails else 0)
