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

boot = float(np.abs(load2("boot")).max())
check("silent at power-on", boot == 0, f"peak {boot:.4f} with no note (rendered before engine_init too)")

bu, bd = peak_hz(*spectrum(load("bend_up"))), peak_hz(*spectrum(load("bend_down")))
check("pitch bend", abs(bu - 880) < 2 and abs(bd - 392) < 1.5, f"full up at +-12: {bu:.1f} Hz (880); full down at +-2: {bd:.1f} Hz (392)")
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
f, s = spectrum(load("mix_fifth"))
a, b = level_db(f, s, 440), level_db(f, s, 440 * 2 ** (7 / 12))
check("mix pitch", a > -3 and b > -3, f"PIT +7: 440 Hz {a:.1f} dB, 659 Hz {b:.1f} dB")
f, s = spectrum(load("mix_fine"))
a, b = level_db(f, s, 440, 3), level_db(f, s, 440 * 2 ** (50 / 1200), 3)
check("mod fine", a > -3 and b > -3, f"FINE +50c: 440 Hz {a:.1f} dB, 453 Hz {b:.1f} dB")
f, s = spectrum(load("menv1_pitch"))
up = peak_hz(f, s)                              # (a full AMT: +11.6 semitones, through the amount's curve)
f, s = spectrum(load("menv2_pitch"))
pk = peak_hz(f, s)
check("menv 2 -> pitch", abs(pk - up) < 1 and pk > 800, f"the note at {pk:.1f} Hz (envelope 1 the same: {up:.1f} Hz)")
f, s = spectrum(load("menv2_m1pitch"))
a, b = level_db(f, s, 440), level_db(f, s, up)
check("menv 2 -> m1pitch", a > -3 and b > -3, f"440 Hz {a:.1f} dB, mod 1 at {up:.0f} Hz {b:.1f} dB")

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
b20, b40 = atten("lp24_1k", "lp_open", 110, 20), atten("lp24_1k", "lp_open", 110, 40)
check("lp 24 slope", -27 < b40 - b20 < -20, f"2.2 -> 4.4 kHz {b40 - b20:.1f} dB (want ~-23, -> 24 dB/oct)")
r24 = atten("lp24_res", "lp_open", 110, 9)
check("lp 24 resonance", r24 > 10, f"990 Hz {r24:+.1f} dB at high resonance")
sc3, sc5, s24 = load("self_c3"), load("self_c5"), load("self_lp24")
l3, l5, l24 = rms(sc3, 0.5, 1.3), rms(sc5, 0.5, 1.3), rms(s24, 0.5, 1.3)
check("self-oscillation", min(l3, l5, l24) > 0.03 and rms(sc5, 1.0, 1.3) > 0.9 * rms(sc5, 0.5, 0.8),
      f"with nothing in, RMS {l3:.3f} (C3) {l5:.3f} (C5) {l24:.3f} (LP 24), held")
check("self-oscillation level", abs(20 * np.log10(l5 / l3)) < 2, f"C5 vs C3 {20 * np.log10(l5 / l3):+.1f} dB")
f3, f5 = peak_hz(*spectrum(sc3, 0.5, 1.3)), peak_hz(*spectrum(sc5, 0.5, 1.3))
check("self-oscillation keytracks", abs(1200 * np.log2(f5 / f3) - 2400) < 15, f"{f3:.0f} -> {f5:.0f} Hz over 2 octaves")
r110 = rms(load("self_res110"), 1.0, 1.3)
check("RES 110 does not self-oscillate", r110 < 0.001, f"RMS {r110:.5f} a second on")
ss = load("self_silent")
fs = peak_hz(*spectrum(ss, 0.5, 1.3))
check("self-oscillation from nothing", rms(ss, 0.5, 1.3) > 0.03 and abs(fs - f5) < 5,
      f"the oscillators at 0 from the note's start: RMS {rms(ss, 0.5, 1.3):.3f}, {fs:.0f} Hz")
sl_ = load("self_silent_low")
fl = peak_hz(*spectrum(sl_, 0.5, 1.3))
check("self-oscillation from nothing, low", rms(sl_, 0.5, 1.3) > 0.03 and abs(1200 * np.log2(fl / 130.81)) < 50,
      f"C3 at CUT 47: {fl:.1f} Hz ({1200 * np.log2(fl / 130.81):+.0f} cents from C3), RMS {rms(sl_, 0.5, 1.3):.3f}")
h2, h4, h40 = atten("hp_880", "lp_open", 110, 2), atten("hp_880", "lp_open", 110, 4), atten("hp_880", "lp_open", 110, 40)
check("high-pass stopband", -28 < h2 < -21, f"220 Hz {h2:.1f} dB (want ~-24.6)")
check("high-pass slope", -12.5 < h2 - h4 < -8.5, f"440 -> 220 Hz {h2 - h4:.1f} dB (want ~-10.6, -> 12 dB/oct)")
check("high-pass passband", -1 < h40 < 0.3, f"4.4 kHz {h40:.1f} dB")
hpk = max(atten("hp_880", "lp_open", 110, k) for k in range(2, 48))
check("high-pass no resonance", hpk < 0.3, f"the most any harmonic gains: {hpk:+.2f} dB")
r8, r2, r40 = (atten("hp_res_quiet", "hp_quiet_ref", 110, k) for k in (8, 2, 40))
check("high-pass resonance", 13 < r8 < 18 and r2 < -10 and -1 < r40 < 1,
      f"HP RES 127: 880 Hz {r8:+.1f} dB (want ~+15.7, k 0.16), 220 Hz {r2:.1f} dB, 4.4 kHz {r40:.1f} dB")
hl = atten("hp_res", "lp_open", 110, 8)
check("high-pass resonance, loud", hl > 8, f"full level, soft-clipped: 880 Hz {hl:+.1f} dB")
cp, cn, ca, co = (harm_db(n, 440, 1, 0.6, 0.9) - harm_db("sine", 440, 1, 0.6, 0.9)
                  for n in ("comb_on", "comb_neg", "comb_abs", "comb_off"))
check("comb on the note", -2 < cp < 1, f"440 Hz {cp:+.1f} dB (a peak, keytrack 100 %)")
check("comb negative", cn < -15, f"440 Hz {cn:+.1f} dB (between the peaks)")
check("comb frequency", -2 < ca < 1 and co < -10, f"no keytrack: 440 Hz {ca:+.1f} dB at FREQ 440 Hz, {co:+.1f} dB at 523 Hz")
s1 = harm_db("comb_spread", 110, 1, 0.8, 1.3) - harm_db("comb_even", 110, 1, 0.8, 1.3)
s30 = np.mean([harm_db("comb_spread", 110, k, 0.8, 1.3) - harm_db("comb_even", 110, k, 0.8, 1.3) for k in range(24, 40)])
check("comb spread", -4 < s1 < 2 and s30 < -6,
      f"the fundamental {s1:+.1f} dB (still in tune), harmonics 24..39 {s30:+.1f} dB (the peaks moved off them)")
lo, lk, lf = (20 * np.log10(rms(load(n), 0.3, 0.8)) for n in ("lp_open", "comb_res127", "comb_res127_off"))
check("comb level", lk > lo - 1 and lf > lo - 15,
      f"full RES: {lk - lo:+.1f} dB on the harmonics, {lf - lo:+.1f} dB off them (vs the open low-pass)")
cs = load("comb_stable")
check("comb stable", np.isfinite(cs).all() and np.abs(cs[int(1.8 * FS):]).max() < 0.01,
      f"peak {np.abs(cs).max():.3f}; after the release {np.abs(cs[int(1.8 * FS):]).max():.4f}")
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

tr = {n: inst_freq(load(n), 0.1, 1.3) for n in ("lfoshape_menv2", "lfoshape_sine", "lfoshape_sqr")}
dm = float(np.nanmax(np.abs(1200 * np.log2(tr["lfoshape_menv2"] / tr["lfoshape_sine"]))))
dq = float(np.nanmax(np.abs(1200 * np.log2(tr["lfoshape_sqr"] / tr["lfoshape_sine"]))))
check("lfoshape: m.env 2", dm < 3 and dq > 20,
      f"a square vibrato, M.ENV 2 on LFOSHAPE at -full: the sine's pitch to {dm:.1f} cents (the square alone: {dq:.0f})")
c_, q_ = load("lfoshape_lfo2"), load("lfoshape_sqr")
check("lfoshape: lfo 2", float(np.max(np.abs(c_ - q_))) > 0.1, "LFO 2 on LFOSHAPE changes the square's vibrato")


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
def trem_hz(name):                              # the tremolo's rate: the envelope's strongest frequency
    e = np.array([rms(load(name), t0, t0 + 0.005) for t0 in np.arange(0.1, 2.9, 0.005)])
    f = np.fft.rfft((e - e.mean()) * np.hanning(len(e)), 16 * len(e))
    return np.fft.rfftfreq(16 * len(e), 0.005)[int(np.argmax(np.abs(f)))]
h4, h16, hf_ = trem_hz("l2_sync4"), trem_hz("l2_sync16"), trem_hz("l2_free")
check("lfo2 sync", 1.95 < h4 < 2.05 and 7.8 < h16 < 8.2, f"at 120 BPM: 1/4 {h4:.2f} Hz (want 2), 1/16 {h16:.2f} Hz (want 8)")
check("lfo2 free rate", 2.8 < hf_ < 2.96, f"{hf_:.2f} Hz (rate 77: 2.88 Hz)")
x = load("l2_lvl1")
env = np.array([rms(x, t0, t0 + 0.01) for t0 in np.arange(0.1, 1.8, 0.01)])
check("lfo2 level", env.min() < 0.2 * env.max(), f"level {env.min() / env.max():.3f} .. 1 of its peak")
pk = [level_db(*spectrum(load("l2_res"), t0, t0 + 0.1), 990, 15) for t0 in np.arange(0.1, 1.8, 0.05)]
check("lfo2 resonance", max(pk) - min(pk) > 8, f"990 Hz (at the cutoff) {min(pk):.1f} .. {max(pk):.1f} dB over the sweep")
ld, l0 = rms(load("l2_dly"), 0.15, 1.2), rms(load("fx_dry"), 0.15, 1.2)
check("lfo2 effect", ld > 20 * (l0 + 1e-6), f"after the note: {ld:.4f} with LFO 2 opening the delay (mix 0), {l0:.5f} dry")
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

# ---- velocity
v127, v40, voff = load("vel_127"), load("vel_40"), load("vel_40_off")
lv = rms(v40, 0.2, 0.8) / rms(v127, 0.2, 0.8)
check("velocity level", 0.05 < lv < 0.2, f"velocity 40 is {lv:.3f} of 127's level (sensitivity full: (40/127)^2 = 0.099)")
def centroid(x):
    f, s = spectrum(x)
    return float((f * s ** 2).sum() / (s ** 2).sum())


c127, c40 = centroid(v127), centroid(v40)
check("velocity brightness", c40 < 0.8 * c127, f"spectral centroid {c127:.0f} Hz at 127, {c40:.0f} Hz at 40")
check("velocity off", np.array_equal(voff, v127), "no sensitivity: velocity 40 gives the same samples as 127")

# ---- the arpeggiator
NOTE = {60: 261.63, 64: 329.63, 67: 392.0, 72: 523.25, 76: 659.26, 79: 783.99}
dc, dx = load("drone_chord"), load("drone_closed")
f, s = spectrum(dc, 0.8, 1.4)
lv = [level_db(f, s, NOTE[m], 6) for m in (60, 64, 67)]
check("vca open: a drone chord", min(lv) > -6 and rms(dc, 0.8, 1.4) > 0.02 and rms(dx, 1.0, 1.4) < 0.0005,
      f"a second after letting go: C4 {lv[0]:.1f}, E4 {lv[1]:.1f}, G4 {lv[2]:.1f} dB; closed: {rms(dx, 1.0, 1.4):.5f}")
di = load("drone_instant")
fi = peak_hz(*spectrum(di, 0.3, 1.3))
check("vca open: an instant drone", rms(di, 0.3, 1.3) > 0.02 and abs(fi - 130.81) < 2, f"no note played: {fi:.1f} Hz (C3)")
dl = load("drone_last")
fl, sl = spectrum(dl, 0.2, 0.65)
fh, sh_ = spectrum(dl, 0.8, 1.3)
pk_, h2a, h2b = peak_hz(fl, sl), level_db(fl, sl, 784, 8), level_db(fh, sh_, 784, 8)
check("vca open: where it was", abs(pk_ - 392) < 3 and h2b > h2a + 20,
      f"opened after G4: {pk_:.0f} Hz; AMT1 turned up while it drones: 2nd harmonic {h2a:.0f} -> {h2b:.0f} dB")
do = load("drone_off")
jump = float(np.max(np.abs(np.diff(do[int(0.79 * 44100):int(0.9 * 44100)]))))
check("vca closed again: it fades", rms(do, 1.0, 1.4) < 0.0005 and jump < 0.05,
      f"after: {rms(do, 1.0, 1.4):.5f}; the largest step meanwhile {jump:.3f}")
STEP = 60 / 120 / 4                              # 1/16 at 120 BPM


def arp_notes(name, n=8, step=STEP):
    x = load(name)
    out = []
    for k in range(n):
        f = peak_hz(*spectrum(x, k * step + 0.012, k * step + 0.055))
        out.append(min(NOTE, key=lambda m: abs(NOTE[m] - f)) if f > 100 else None)
    return out


for name, want in (("arp_up", [60, 64, 67] * 3), ("arp_down", [67, 64, 60] * 3), ("arp_updn", [60, 64, 67, 64] * 2 + [60]),
                   ("arp_oct2", [60, 64, 67, 72, 76, 79, 60, 64, 67])):
    got = arp_notes(name, len(want))
    check(name.replace("_", " "), got == want, f"{got}")


def onsets(x, thr=0.02):
    e = np.abs(x)
    on, i = [], 0
    while i < len(e):
        if e[i] > thr:
            on.append(i)
            i += int(0.03 * FS)                   # (past this note's attack)
            while i < len(e) and np.max(e[i:i + 64]) > thr * 0.5: i += 64
        i += 1
    return np.array(on)


ons = onsets(load("arp_up")[: int(1.35 * FS)])
d = np.diff(ons)
check("arp timing", len(d) >= 8 and np.all(np.abs(d - STEP * FS) <= 24), f"steps {d.min()}..{d.max()} frames (want {STEP * FS:.1f}, +-a block)")
gap_rms = rms(load("arp_up"), 0.10, 0.12)           # in the second half of the first step: the 50 % gate is closed
tie_rms = rms(load("arp_tie"), 0.10, 0.12)
check("arp gate", gap_rms < 0.1 * tie_rms, f"between notes {gap_rms:.4f} with a 50 % gate, {tie_rms:.4f} tied")
sw = np.diff(onsets(load("arp_swing")[: int(1.35 * FS)]))
ratio = np.median(sw[0::2]) / np.median(sw[1::2]) if len(sw) > 3 else 0
check("arp swing", 2.6 < ratio < 3.4, f"steps alternate {np.median(sw[0::2]):.0f} / {np.median(sw[1::2]):.0f} frames (ratio {ratio:.2f}, want 3)")
la, nl = rms(load("arp_latch"), 0.8, 1.3), rms(load("arp_nolatch"), 0.8, 1.3)
check("arp latch", la > 0.02 and nl < 0.001, f"after the keys are let go: {la:.4f} latched, {nl:.5f} not")

# ---- the arp on the MIDI clock
def starts(x, lo=0, hi=None):
    """note starts: the 64-frame envelope rising through 0.02 (the notes are gated 50 %: silence between)"""
    hi = hi or len(x)
    env = np.array([np.abs(x[i:i + 64]).max() for i in range(lo, hi, 64)])
    out = []
    for i in range(1, len(env)):
        if env[i] > 0.02 and env[i - 1] <= 0.02:
            j = lo + (i - 1) * 64                 # the exact frame: the first above in that window
            seg = np.abs(x[j:j + 128])
            out.append(j + int(np.argmax(seg > 0.02)))
    return np.array(out)


mcx = load("mclock")
ticks, t, per = [], 1100.0, 918.75
while t < 3.5 * FS:
    ticks.append(t)
    t += per
    if t > 2.0 * FS and per < 1000: per = 1102.5
want = np.array([ticks[k] + 248 for k in range(0, len(ticks), 6)])      # every 6th tick, one render later
got = starts(mcx, 0, int(3.49 * FS))
n = min(len(want), len(got))
err = got[:n] - want[:n]
check("midi clock steps", n >= 20 and np.all(np.abs(err) <= 24), f"{n} steps, each {err.min():+.0f}..{err.max():+.0f} frames from its tick (+ one render)")
sp1 = np.median(np.diff(got[got < 1.9 * FS])); sp2 = np.median(np.diff(got[(got > 2.3 * FS) & (got < 3.4 * FS)]))
check("midi clock tempo", abs(sp1 - 5512.5) < 20 and abs(sp2 - 6615) < 20, f"steps {sp1:.0f} frames at 120 BPM, {sp2:.0f} at 100 BPM")
quiet = rms(mcx, 3.6, 3.95)
after = arp_notes("mclock", 1) if False else None
f_restart = peak_hz(*spectrum(mcx, 4.0 + (100 + 248) / FS + 0.01, 4.0 + (100 + 248) / FS + 0.05))
check("midi clock stop / start", quiet < 0.001 and abs(f_restart - 261.6) < 8,
      f"after STOP {quiet:.5f}; after START the first note {f_restart:.0f} Hz (C4, the pattern from the top)")
bpm10 = int(open(D / "mclock.txt").read())
check("midi clock bpm", 990 <= bpm10 <= 1010, f"the clock reads {bpm10 / 10:.1f} BPM (the last ticks: 100)")
sw = starts(load("mclock_swing"), 0, int(1.9 * FS))
d = np.diff(sw)
ratio = np.median(d[0::2]) / np.median(d[1::2]) if len(d) > 4 else 0
check("midi clock swing", 2.6 < ratio < 3.4, f"steps alternate {np.median(d[0::2]):.0f} / {np.median(d[1::2]):.0f} frames (want 3:1)")

# ---- the sequencer
NOTE.update({48: 130.81, 52: 164.81, 55: 196.0})


def seq_notes(name, n, step=STEP, t0=0.0):
    x = load(name)
    out = []
    for k in range(n):
        a = t0 + k * step
        if rms(x, a + 0.01, a + 0.05) < 0.005:
            out.append(0); continue
        f = peak_hz(*spectrum(x, a + 0.008, a + 0.058))
        out.append(min(NOTE, key=lambda m: abs(NOTE[m] - f)))
    return out


want = [48, 52, 55, 0, 60, 0, 55, 52] * 2
got = seq_notes("seq", 16)
check("seq notes", got == want, f"{got}")
check("seq length", seq_notes("seq_len3", 9) == [48, 52, 55] * 3, f"{seq_notes('seq_len3', 9)}")
on = starts(load("seq"), 0, int(1.95 * FS))
d = np.diff(on)
check("seq timing", len(d) >= 8 and np.all(np.abs((d / (STEP * FS)) - np.round(d / (STEP * FS))) * STEP * FS <= 24),
      f"{len(on)} notes, each on its 16th (5512.5 frames) to {np.max(np.abs((d / (STEP * FS)) - np.round(d / (STEP * FS))) * STEP * FS):.0f} frames")
xa = load("seq_accent")
acc, nrm = rms(xa, 0.01, 0.05), rms(xa, STEP + 0.01, STEP + 0.05)
check("seq accent", acc > 1.4 * nrm, f"an accent {acc / nrm:.2f} x a plain step's level (127 vs 100: x {127 / 100:.2f}^2 = {(127 / 100) ** 2:.2f})")
f, s = spectrum(load("seq_chord"), 0.005, 0.12)      # (C4 E4 G4: 68 Hz and more apart, 9 Hz bins)
lv = [level_db(f, s, NOTE[m], 6) for m in (60, 64, 67)]
gap = max(level_db(f, s, hz, 6) for hz in (296, 361))   # between them: nothing
check("seq chord", min(lv) > -6 and gap < min(lv) - 20 and rms(load("seq_chord"), STEP + 0.03, 4 * STEP) < 0.005,
      f"step 1: C4 {lv[0]:.1f}, E4 {lv[1]:.1f}, G4 {lv[2]:.1f} dB (between: {gap:.1f}); then silent")
lt = load("seq_lane_tie")
f, s = spectrum(lt, 2 * STEP + 0.005, 2 * STEP + 0.055)   # (steady: its gate ends later in the step)
g4, c4 = level_db(f, s, NOTE[67], 12), level_db(f, s, NOTE[60], 12)
check("seq lane tie", g4 > -3 and c4 < g4 - 40 and rms(lt, 3 * STEP + 0.06, 4 * STEP) < 0.005,
      f"step 3: G4 {g4:.1f} dB held, C4 {c4:.1f} dB gone; step 4 silent")
ck = load("seq_click")
bar, beat = peak_hz(*spectrum(ck, 0.0, 0.03)), peak_hz(*spectrum(ck, 4 * STEP, 4 * STEP + 0.03))
check("metronome", abs(bar - 2000) < 30 and abs(beat - 1000) < 30 and rms(ck, 2 * STEP, 3 * STEP) < 0.0005 and
      rms(load("seq"), 0.2, 0.3) > 0, f"beat 1: {bar:.0f} Hz, beat 2: {beat:.0f} Hz, silent between")
sl, ns = load("seq_slide"), load("seq_noslide")
gap_s = min(rms(sl, t, t + 0.005) for t in np.arange(0.06, 0.13, 0.005))
gap_n = min(rms(ns, t, t + 0.005) for t in np.arange(0.06, 0.13, 0.005))
check("seq slide holds", gap_s > 0.05 and gap_n < 0.005, f"lowest level between steps 1 and 2: {gap_s:.3f} sliding, {gap_n:.4f} not")
def cycles(x, t0, t1):
    """(time, frequency) of each cycle: from one rising zero crossing to the next"""
    s = x[int(t0 * FS):int(t1 * FS)]
    z = np.where((s[:-1] <= 0) & (s[1:] > 0))[0]
    z = z + s[z] / (s[z] - s[z + 1])
    return [(t0 + (a + b) / 2 / FS, FS / (b - a)) for a, b in zip(z[:-1], z[1:])]


cs = cycles(sl, STEP - 0.02, STEP + 0.06)
before = [f for t, f in cs if t < STEP]
during = [f for t, f in cs if STEP + 0.004 < t < STEP + 0.02]
after = [f for t, f in cs if STEP + 0.045 < t]
cn = [f for t, f in cycles(ns, STEP, STEP + 0.03)]
check("seq slide glides", abs(before[-1] - 130.8) < 3 and all(140 < f < 194 for f in during) and abs(np.median(after) - 196) < 3
      and abs(cn[1] - 196) < 4,
      f"C3 {before[-1]:.0f} Hz, then {', '.join(f'{f:.0f}' for f in during)} sliding, G3 {np.median(after):.0f} Hz;"
      f" without the slide {cn[1]:.0f} Hz at once")
xt = load("seq_tie")
held = min(rms(xt, a, a + 0.005) for a in np.arange(0.02, 2.4 * STEP, 0.005))   # (the last tied step: its own 50 % gate)
gone = rms(xt, 3 * STEP + 0.03, 4 * STEP - 0.005)
again = len(starts(xt, int(0.01 * FS), int(2.4 * STEP * FS)))
check("seq tie", held > 0.02 and gone < 0.005 and again == 0,
      f"C3 held through steps 1-3 (lowest {held:.3f}, no new attack: {again}), the rest after silent ({gone:.4f})")
got = seq_notes("seq_song", 12)
check("seq song", got == ([48] * 4 + [55] * 2) * 2, f"{got} (P1 twice, P2 once, round)")
xs = load("seq_clock")
quiet = rms(xs, 0.1, 0.45)
first = seq_notes("seq_clock", 4, t0=0.5 + (100 + 248) / FS)
check("seq midi clock", quiet < 0.001 and first == [48, 52, 56 if 56 in NOTE else first[2], 60],
      f"silent before START ({quiet:.4f}); then {first} from step 1")

# ---- glide
def pitch_at(name, t):
    return peak_hz(*spectrum(load(name), t, t + 0.03))


ga = [pitch_at("glide_always", t) for t in (0.36, 0.45, 0.8)]
gl = [pitch_at("glide_legato_apart", t) for t in (0.36, 0.45, 0.8)]
go = [pitch_at("glide_legato_over", t) for t in (0.36, 0.45, 0.8)]
check("glide always", ga[0] < 360 and abs(ga[2] - 392) < 4, "G4 from " + " -> ".join(f"{v:.0f}" for v in ga) + " Hz")
check("glide legato apart", abs(gl[0] - 392) < 8, f"G4 played apart from C4: {gl[0]:.0f} Hz at once (no slide)")
check("glide legato over", go[0] < 360 and abs(go[2] - 392) < 4, "G4 over C4: " + " -> ".join(f"{v:.0f}" for v in go) + " Hz")

# ---- the mod envelope
f = cents(inst_freq(load("menv_rate"), 0.05, 2.85))


def vib_rate(seg):
    s = seg - np.nanmean(seg)
    z = np.where(np.diff(np.sign(np.nan_to_num(s))) > 0)[0]
    return (len(z) - 1) / ((z[-1] - z[0]) * 0.02) if len(z) > 2 else 0.0


r0, r1 = vib_rate(f[:40]), vib_rate(f[100:])
check("menv -> lfo rate", r1 > 3 * max(r0, 0.3), f"vibrato {r0:.1f} Hz in the first 0.8 s, {r1:.1f} Hz after 2 s")
fl = cents(inst_freq(load("menv_loop"), 0.02, 1.48))
fo = cents(inst_freq(load("menv_once"), 0.02, 1.48))


def peaks(x, thr):
    x = np.nan_to_num(x)
    return int(np.sum((x[1:-1] > x[:-2]) & (x[1:-1] >= x[2:]) & (x[1:-1] > thr)))


pl, po = peaks(fl, 200), peaks(fo, 200)
check("menv loop", pl >= 8 and po <= 1, f"{pl} attack-decay cycles in 1.5 s with LOOP, {po} without")
after = cents(inst_freq(load("menv_loop"), 1.7, 2.1))   # (the amp still releasing, the mod envelope done)
check("menv loop ends", np.isfinite(after).sum() > 10 and np.nanmax(np.abs(after)) < 30,
      f"{np.nanmax(np.abs(after)):.0f} cents from the note 0.2-0.6 s after the release")
fd = cents(inst_freq(load("menv_depth"), 0.05, 2.85))
d0, d1 = np.nanmax(np.abs(fd[2:20])), np.nanmax(np.abs(fd[110:]))
check("menv -> lfo depth", d0 > 70 and d1 < 0.3 * d0, f"vibrato +-{d0:.0f} cents at first, +-{d1:.0f} once the envelope is up (amount -full)")

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



def octave_db(x, lo, t0=0.2, t1=0.9):         # the power in the octave from lo, dB
    seg = x[int(t0 * FS):int(t1 * FS)]
    p = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / FS)
    return 10 * np.log10(p[(f >= lo) & (f < 2 * lo)].sum() + 1e-20)


tilt = {}                                       # the noise: 4 octaves apart (250 Hz, 4 kHz), as each colour should be
for name, want, tol in (("white", 12, 3), ("pink", 0, 3), ("brown", -12, 4)):
    x = load("noise_" + name)
    tilt[name] = octave_db(x, 4000) - octave_db(x, 250)
    check(f"noise {name} colour", abs(tilt[name] - want) < tol, f"4 kHz vs 250 Hz octave {tilt[name]:+.1f} dB (want {want:+d})")
ref = rms(load("noise_white"), 0.2, 0.9)
lv = {t: 20 * np.log10(rms(load("noise_" + t), 0.2, 0.9) / ref) for t in ("pink", "brown", "digi", "sh")}
check("noise levels", all(abs(v) < 2.5 for v in lv.values()), "vs WHITE: " + ", ".join(f"{t} {v:+.1f} dB" for t, v in lv.items()))
dk = octave_db(load("noise_dark"), 4000) - octave_db(load("noise_white"), 4000)
check("noise tone", dk < -15, f"COLOUR 30: the 4 kHz octave {dk:+.1f} dB under WHITE's")
x = load("noise_digi")[int(0.2 * FS):int(0.9 * FS)]
per = FS / 440.0
ac = lambda lag: float(np.dot(x[:-2000], np.roll(x, -int(round(lag)))[:-2000]) / np.dot(x[:-2000], x[:-2000]))
check("noise digi plays the note", ac(per) > 0.6 and ac(per / 2) < 0.6, f"autocorrelation a period of A4 on: {ac(per):.2f} (half: {ac(per / 2):.2f})")
f, sdu = spectrum(load("noise_digi_up"))
f0, sd0 = spectrum(load("noise_digi"))
check("noise digi rate", level_db(f, sdu, 880, 4) > -25 and level_db(f, sdu, 440, 4) < level_db(f0, sd0, 440, 4) - 15,
      f"RATE +12 st: 880 Hz {level_db(f, sdu, 880, 4):.1f} dB, 440 Hz {level_db(f, sdu, 440, 4):.1f} dB")
f, ss = spectrum(load("noise_sh"))
null, body = level_db(f, ss, 7040, 60), level_db(f, ss, 2000, 400)
check("noise s&h step", null < body - 15, f"16 x A4 (7040 Hz) {null:.1f} dB, a null; 2 kHz {body:.1f} dB")
x = load("noise_crackle")[int(0.2 * FS):int(0.9 * FS)]
crest = np.abs(x).max() / np.sqrt(np.mean(x ** 2))
clicks = int(np.sum((np.abs(x[1:]) > 0.05) & (np.abs(x[:-1]) <= 0.05)))
check("noise crackle", crest > 6 and 50 < clicks < 2000, f"peak / RMS {crest:.1f}, {clicks} clicks in 0.7 s")

for src in ("", "n"):                           # the routing: DIRECT past a nearly closed filter
    a = octave_db(load(f"route_{src}filter"), 2000)
    b = octave_db(load(f"route_{src}direct"), 2000)
    check(f"route {'noise' if src else 'osc 1'} direct", b - a > 30, f"2 kHz octave {a:.1f} dB filtered, {b:.1f} dB DIRECT")


def onsets(x, lo, hi, hop=220):                 # the times a band's energy jumps
    e = []
    for i in range(0, len(x) - 1024, hop):
        p = np.abs(np.fft.rfft(x[i:i + 1024] * np.hanning(1024))) ** 2
        fr = np.fft.rfftfreq(1024, 1 / FS)
        e.append(p[(fr >= lo) & (fr < hi)].sum())
    d = np.diff(np.array(e))
    return [(i * hop + 512) / FS for i in range(1, len(d) - 1) if d[i] > 0.3 * d.max() and d[i] >= d[i - 1] and d[i] >= d[i + 1]]


def near(ts, want, tol=0.03):
    return all(any(abs(t - w) < tol for t in ts) for w in want)


dr = load2("drums")
x = dr[:, 0]
k, sn, h = onsets(x, 30, 120), onsets(x, 1500, 5000), onsets(x, 8000, 11000)
check("drums kick", near(k, [0, 0.5]) and not near(k, [0.25]), f"onsets {[round(t, 3) for t in k]} (0, 0.5)")
check("drums snare", near(sn, [0.25, 0.75]), f"onsets {[round(t, 3) for t in sn][:6]} (0.25, 0.75)")
check("drums hat", near(h, [i * 0.125 for i in range(1, 8)]), f"onsets {[round(t, 3) for t in h][:8]} (every 0.125)")
pk = np.abs(dr).max()
check("drums level", 0.15 < pk < 0.8 and np.abs(dr[:, 0] - dr[:, 1]).max() == 0, f"peak {pk:.3f}, the same both sides")
oh = 10 ** ((octave_db(x, 6000, 0.42, 0.49) - octave_db(x, 6000, 0.17, 0.24)) / 20)   # (the hat's band, 45-115 ms
check("drums open hat", oh > 3, f"45-115 ms after the accent: {oh:.1f} x the closed hat's ring")   # after step 4 / step 2)
tl = rms(load("drums"), 1.5, 2.0), rms(load("drums_rev"), 1.5, 2.0)
check("drums reverb send", tl[0] < 1e-4 and tl[1] > 0.005, f"RMS 0.5 s after the last hit: {tl[0]:.5f} dry, {tl[1]:.4f} sent")
hc = octave_db(load("drums_crush"), 8000, 0, 1) - octave_db(load("drums_crush"), 1000, 0, 1)
hp = octave_db(load("drums"), 8000, 0, 1) - octave_db(load("drums"), 1000, 0, 1)
check("drums crush", np.abs(load("drums_crush")[int(1.5 * FS):]).max() < 0.002 and abs(hc - hp) > 3,
      f"8k vs 1k octave {hc:+.1f} dB crushed, {hp:+.1f} dB plain; silent after (no offset)")
km = onsets(load("drums_midi"), 30, 120)
check("drums midi", near(km, [0, 22016 / FS]), f"kick onsets {[round(t, 3) for t in km]} (0, 0.499)")
kt = load("drums_tune")
f, s1 = spectrum(load("drums"), 0.0, 0.2)
f2, s2 = spectrum(kt, 0.0, 0.2)
c1, c2 = (f[f < 400] * s1[f < 400] ** 2).sum() / (s1[f < 400] ** 2).sum(), (f2[f2 < 400] * s2[f2 < 400] ** 2).sum() / (s2[f2 < 400] ** 2).sum()
check("drums tune", c2 > 1.4 * c1, f"the kick's low centroid {c1:.0f} Hz -> {c2:.0f} Hz an octave up")
d1, d2 = rms(load("drums"), 0.10, 0.12), rms(kt, 0.10, 0.12)
check("drums decay", d2 < 0.5 * d1, f"100 ms in: {d2:.4f} with DECAY 40, {d1:.4f} as made")

fl = load("drums_filter")
hf = octave_db(fl, 8000, 0, 1) - octave_db(load("drums"), 8000, 0, 1)
kf = octave_db(fl, 63, 0, 1) - octave_db(load("drums"), 63, 0, 1)
check("drums filter", hf < -20 and abs(kf) < 0.5 and np.abs(fl[int(1.5 * FS):]).max() < 0.002,
      f"the snare and hat through it: 8k octave {hf:+.1f} dB; the kick (not sent) {kf:+.2f} dB; silent after")

sp, fd = load("drums_snare"), load("drums_flam") - load("drums_snare")   # (the bus is linear: the difference is the second)
first = lambda y: int(np.argmax(np.abs(y[int(0.2 * FS):int(0.5 * FS)]) > 1e-4))
on = first(fd) - first(sp)
ratio = np.abs(fd[int(0.2 * FS):int(0.5 * FS)]).max() / np.abs(sp[int(0.2 * FS):int(0.5 * FS)]).max()
check("drums flam", 870 <= on <= 890 and 0.2 < ratio < 0.8,
      f"the second snare {on} frames after the first (880), at {ratio:.2f} of its peak; the first rings on under it")

print("all passed" if not fails else f"{len(fails)} failed: {', '.join(fails)}")
sys.exit(1 if fails else 0)
