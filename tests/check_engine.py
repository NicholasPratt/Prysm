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


def load2(name):                                 # both channels
    with wave.open(str(D / f"{name}.wav")) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2)
    return x.astype(float) / 32768


def load(name):
    return load2(name)[:, 0]


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

x = load("cut_jump")                            # brightness in 2 ms windows after the jump at ~499 ms


def hf(t0):
    seg = x[int(t0 * FS):int((t0 + 0.002) * FS)]
    return float(np.sqrt(np.mean(np.diff(seg) ** 2)))   # the slope's energy: rises with the cutoff


t_jump = 499.0 / 1000
steps = [hf(t_jump + 0.002 * k) for k in range(30)]   # 60 ms
final, start = hf(0.8), hf(0.45)
reached = [s >= start + 0.9 * (final - start) for s in steps]
first = reached.index(True) if True in reached else None
check("cutoff glides", first is not None and 3 <= first <= 25,
      f"90 % of the change after {first * 2} ms (instant: 0-2 ms; want 6-50)" if first is not None
      else "never reached 90 % within 60 ms")

# ---- the LFO
def inst_freq(x, t0, t1, win=0.02):
    """frequency in 20 ms windows from zero crossings (rising), Hz"""
    out = []
    for ts in np.arange(t0, t1, win):
        s = x[int(ts * FS):int((ts + win) * FS)]
        z = np.where((s[:-1] <= 0) & (s[1:] > 0))[0]
        if len(z) < 3:
            out.append(np.nan); continue
        fr = z + s[z] / (s[z] - s[z + 1])   # interpolated crossings
        out.append((len(fr) - 1) / ((fr[-1] - fr[0]) / FS))
    return np.array(out)


def cents(f):
    return 1200 * np.log2(f / 440)


fv = cents(inst_freq(load("lfo_vib"), 0.1, 1.8))
check("lfo vibrato depth", 80 < np.nanmax(fv) < 130 and -130 < np.nanmin(fv) < -80,
      f"{np.nanmin(fv):+.0f} .. {np.nanmax(fv):+.0f} cents (want +-100)")
z = np.where(np.diff(np.sign(fv - np.nanmean(fv))) > 0)[0]
rate = (len(z) - 1) / ((z[-1] - z[0]) * 0.02) if len(z) > 2 else 0
check("lfo vibrato rate", 4.5 < rate < 5.5, f"{rate:.2f} Hz (want 5.0)")
fd = cents(inst_freq(load("lfo_delay"), 0.0, 1.8))
early, late = np.nanmax(np.abs(fd[:5])), np.nanmax(np.abs(fd[60:]))
check("lfo delay", early < 25 and late > 80, f"{early:.0f} cents in the first 100 ms, {late:.0f} after 1.2 s")
fs_ = cents(inst_freq(load("lfo_sh"), 0.05, 1.85))
levels = np.unique(np.round(fs_[np.isfinite(fs_)] / 25))
steps = np.sum(np.abs(np.diff(fs_[np.isfinite(fs_)])) > 50)
check("lfo s&h steps", 4 <= len(levels) and 2 <= steps <= 6, f"{steps} jumps over 1.8 s at 2 Hz, {len(levels)} levels")
x = load("lfo_cut")
br = [float((spectrum(x, t0, t0 + 0.05)[1][int(1500 / FS * 0.05 * FS):] ** 2).sum()) for t0 in np.arange(0.1, 1.8, 0.05)]
check("lfo cutoff", max(br) > 5 * (min(br) + 1e-9), f"bright energy {min(br):.2f} .. {max(br):.2f} over the cycle")
x = load("lfo_amp")
env = np.array([rms(x, t0, t0 + 0.01) for t0 in np.arange(0.1, 1.8, 0.01)])
check("lfo tremolo", env.min() < 0.1 * env.max(), f"level {env.min() / env.max():.3f} .. 1 of its peak")
x = load("lfo_wave1")
h = [level_db(*spectrum(x, t0, t0 + 0.1), 1320) for t0 in (0.05, 0.55, 1.05, 1.55)]   # 1.04 Hz square: + - + -
check("lfo wave position", max(h) - min(h) > 30, "3rd harmonic " + ", ".join(f"{v:.0f}" for v in h) + " dB by half cycle")
x = load("lfo_mod1")
h = [level_db(*spectrum(x, t0, t0 + 0.1), 880) for t0 in (0.05, 0.55, 1.05, 1.55)]
check("lfo mod amount", max(h) - min(h) > 10, "2nd harmonic " + ", ".join(f"{v:.0f}" for v in h) + " dB by half cycle")
a0, a3 = load("rt_on_0"), load("rt_on_333")
n = int(0.6 * FS); o = int(0.333 * FS) // 256 * 256
same_on = np.max(np.abs(a0[:n] - a3[o:o + n]))
b0, b3 = load("rt_off_0"), load("rt_off_333")
same_off = np.max(np.abs(b0[:n] - b3[o:o + n]))
check("lfo retrig", same_on < 1e-3 and same_off > 0.01,
      f"a later note: {same_on:.5f} apart with retrig, {same_off:.3f} without (free-running)")

# ---- the effects
dry, byp = load2("fx_dry"), load2("fx_bypass")
check("fx bypass", np.array_equal(dry, byp), "all mixes 0: the same samples as no effects")


def env_ms(x, step=0.005):
    return np.array([np.sqrt(np.mean(x[int(t * FS):int((t + step) * FS)] ** 2)) for t in np.arange(0, len(x) / FS - step, step)])


d = load2("fx_dly")
e = env_ms(d[:, 0])                             # the repeat spacing: the envelope's autocorrelation peak, 100..400 ms
ac = np.correlate(e - e.mean(), e - e.mean(), "full")[len(e) - 1:]
lag = (20 + int(np.argmax(ac[20:80]))) * 5
check("delay time", 195 <= lag <= 210, f"repeats every {lag} ms (param 34: 202 ms)")
lv = [e[(k * 202) // 5:(k * 202 + 60) // 5].max() for k in range(1, 5)]
check("delay feedback", lv[1] < lv[0] and lv[2] < lv[1] and lv[3] < lv[2], "repeats " + ", ".join(f"{v:.3f}" for v in lv))
pp = load2("fx_ping")
el, er = env_ms(pp[:, 0]), env_ms(pp[:, 1])
w1, w2 = slice(int(0.19 / 0.005), int(0.30 / 0.005)), slice(int(0.39 / 0.005), int(0.50 / 0.005))   # repeats 1, 2
check("ping-pong", el[w1].max() > 3 * er[w1].max() and er[w2].max() > 3 * el[w2].max(),
      f"repeat 1 L/R {el[w1].max():.3f}/{er[w1].max():.3f}, repeat 2 L/R {el[w2].max():.3f}/{er[w2].max():.3f}")
sh = load("fx_shift")                          # (the shifter is in the feedback: repeat 1 is the note as played)
f1, f2, f3 = (peak_hz(*spectrum(sh, k * 0.202 + 0.005, k * 0.202 + 0.055)) for k in (1, 2, 3))
check("delay shift", abs(f1 - 440) < 25 and abs(f2 - 880) < 40 and abs(f3 - 1760) < 80,
      f"repeats {f1:.0f}, {f2:.0f}, {f3:.0f} Hz (want 440, 880, 1760)")
x = load("fx_wow")
wet = x - load("fx_dry")[:len(x)] * 0                      # (the held note is in it too: look at the spread)
f, s = spectrum(x, 0.4, 1.3)
spread = np.sum(s[(np.abs(f - 440) > 3) & (np.abs(f - 440) < 30)] ** 2) / np.sum(s ** 2)
check("delay wow", spread > 0.002, f"{spread * 100:.2f} % of the energy in sidebands around 440 Hz")
c2 = load2("fx_cho")
diff = np.sqrt(np.mean((c2[int(0.3 * FS):, 0] - c2[int(0.3 * FS):, 1]) ** 2)) / np.sqrt(np.mean(c2[int(0.3 * FS):, 0] ** 2))
check("chorus stereo", diff > 0.1, f"L - R is {diff:.2f} of L (the dry note alone: 0)")


def rt(x, t0=0.1):
    e = env_ms(x, 0.01)
    db = 20 * np.log10(e / e[int(t0 / 0.01):].max() + 1e-9)
    i0 = int(t0 / 0.01) + np.argmax(e[int(t0 / 0.01):])
    below = np.where(db[i0:] < -30)[0]
    return (below[0] if len(below) else len(db) - i0) * 0.01


rs, rb = rt(load("fx_rev_small")), rt(load("fx_rev_big"))
check("reverb size", rb > 2 * rs and rb > 0.8, f"-30 dB after {rs:.2f} s (size 40), {rb:.2f} s (size 120)")
rv = load2("fx_rev_big")
seg = rv[int(0.3 * FS):int(1.2 * FS)]
corr = np.corrcoef(seg[:, 0], seg[:, 1])[0, 1]
check("reverb stereo", abs(corr) < 0.5, f"L/R correlation {corr:.2f} in the tail")
fb_, sb = spectrum(load("fx_rev_held"), 1.0, 2.5)   # a held sine: a still reverb gives back a pure sine
fm_, sm = spectrum(load("fx_rev_held_mod"), 1.0, 2.5)
w_b = np.sum(sb[(np.abs(fb_ - 440) > 3) & (np.abs(fb_ - 440) < 40)] ** 2) / np.sum(sb ** 2)
w_m = np.sum(sm[(np.abs(fm_ - 440) > 3) & (np.abs(fm_ - 440) < 40)] ** 2) / np.sum(sm ** 2)
check("reverb wobble", w_m > 10 * w_b and w_m > 0.001, f"energy beside 440 Hz: {w_b * 100:.4f} % still, {w_m * 100:.3f} % swept")
shm = load("fx_shimmer")
o8 = level_db(*spectrum(shm, 1.0, 2.0), 880) - level_db(*spectrum(load("fx_rev_big"), 1.0, 2.0), 880)
tail = np.abs(shm[int(2.5 * FS):]).max()
check("shimmer", o8 > 6 and tail < 0.9, f"880 Hz in the tail {o8:+.1f} dB over plain; peak at 2.5 s {tail:.3f}")
al = load2("fx_all")
check("all effects stable", np.isfinite(al).all() and np.abs(al[int(3.5 * FS):]).max() < 0.99,
      f"peak {np.abs(al).max():.3f}; at 3.5 s {np.abs(al[int(3.5 * FS):]).max():.3f}")

x = load("chord")
pk = np.abs(x).max()
check("chord headroom", pk < 0.999, f"peak {pk:.3f} of full scale")

print("all passed" if not fails else f"{len(fails)} failed: {', '.join(fails)}")
sys.exit(1 if fails else 0)
