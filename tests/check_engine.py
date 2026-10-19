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

def harm_db(name, f0, k, t0=0.2, t1=0.8):
    """harmonic k of f0 in the render, dB (absolute: relative to full scale over the window)"""
    x = load(name)[int(t0 * FS):int(t1 * FS)]
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) / (len(x) / 4)
    f = np.fft.rfftfreq(len(x), 1 / FS)
    m = np.abs(f - f0 * k) <= 3
    return 20 * np.log10(s[m].max() + 1e-12)


def atten(name, ref, f0, k):
    return harm_db(name, f0, k) - harm_db(ref, f0, k)


# (the waves hold 48 harmonics: measure within them. Theory for k = 2 at 987 Hz: -1.5, -15.5, -26.4 dB)
a4, a20, a40 = atten("lp_1k", "lp_open", 110, 4), atten("lp_1k", "lp_open", 110, 20), atten("lp_1k", "lp_open", 110, 40)
check("filter passband", -3 < a4 < 0.5, f"440 Hz {a4:.1f} dB (want ~-1.5)")
check("filter stopband", -32 < a40 < -21, f"4.4 kHz {a40:.1f} dB (want ~-26)")
check("filter slope", -13 < a40 - a20 < -9, f"2.2 -> 4.4 kHz {a40 - a20:.1f} dB (want ~-11, -> 12 dB/oct)")
r9 = atten("lp_res", "lp_open", 110, 9)
check("filter resonance", r9 > 6, f"990 Hz {r9:+.1f} dB at high resonance")
lo = atten("key_on_lo", "lp_open", 110, 8)
hi = atten("key_on_hi", "key_open_hi", 880, 8)
off = atten("key_off_hi", "key_open_hi", 880, 8)
check("keytrack follows", abs(lo - hi) < 3, f"8th harmonic: {lo:.1f} dB on A2, {hi:.1f} dB on A5")
check("keytrack off", off < hi - 10, f"A5 without keytrack {off:.1f} dB")
x = load("fenv")


def bright2(t0, t1):
    f, s = spectrum(x, t0, t1)
    return float((s[f > 1000] ** 2).sum() / (s ** 2).sum())


e0, e1 = bright2(0.0, 0.06), bright2(0.6, 0.85)
check("filter envelope", e0 > 5 * e1, f"energy above 1 kHz: {e0:.3f} at 0-60 ms, {e1:.4f} at 600-850 ms")
f, s = spectrum(load("drive"))
h3 = level_db(f, s, 1320)
check("drive", h3 > -30, f"3rd harmonic {h3:.1f} dB with full drive (clean: < -50)")
x = load("stable")
full = float(np.mean(np.abs(x[:int(0.7 * FS)]) > 0.999))
tail = float(np.abs(x[int(1.4 * FS):]).max())
check("filter stable", np.isfinite(x).all() and full < 0.2 and tail < 0.01,
      f"{full * 100:.1f} % of samples at full scale, tail peak {tail:.4f}")

x = load("chord")
pk = np.abs(x).max()
check("chord headroom", pk < 0.999, f"peak {pk:.3f} of full scale")

print("all passed" if not fails else f"{len(fails)} failed: {', '.join(fails)}")
sys.exit(1 if fails else 0)
