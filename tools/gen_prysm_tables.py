#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate firmware/gen/prysm_tables.h: Prysm's wavetables and the engine's lookup tables.

  tools/gen_prysm_tables.py          (needs numpy; the header is checked in, rerun after a change here)

The chip has no FPU, so everything with a float in it is computed here and written out as integers.

Wavetables: 6 banks x 8 waves x 256 samples, int16. Each wave is drawn at a high resolution, then band-limited to
the first HARM harmonics (an FFT, the rest dropped) so it does not alias at ordinary pitches, then scaled to the
same peak. A bank goes from simple (its first wave) to complex (its last); the engine morphs between neighbours.
"""
from pathlib import Path

import numpy as np

N = 256                 # samples per wave
HI = 4096               # the resolution the shapes are drawn at
HARM = 48               # harmonics kept (C6 x 48 is above 50 kHz: high notes alias a little on the brightest waves)
PEAK = 32000
FS = 44100
BLK = 16                # frames per envelope step (engine.c BLK)

OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "prysm_tables.h"
x = np.arange(HI) / HI  # one cycle, 0..1


def band_limit(shape, harm=HARM):
    """the shape's first `harm` harmonics, resampled to N points, DC removed, peak PEAK"""
    spec = np.fft.rfft(shape)
    keep = np.zeros(N // 2 + 1, complex)
    keep[1:harm + 1] = spec[1:harm + 1] * (N / HI)
    w = np.fft.irfft(keep, N)
    return w / np.max(np.abs(w)) * PEAK


def additive(amps, phases=None):
    """a wave from harmonic amplitudes (amps[0] = the fundamental)"""
    w = np.zeros(HI)
    for h, a in enumerate(amps, 1):
        if a:
            w += a * np.sin(2 * np.pi * h * x + (0 if phases is None else phases[h - 1]))
    return w


saw = 1 - 2 * x
tri = 1 - 4 * np.abs(x - 0.5)
sq = np.where(x < 0.5, 1.0, -1.0)
sine = np.sin(2 * np.pi * x)
pulse = lambda w: np.where(x < w, 1.0, -1.0)

# 1 BASIC: sine -> triangle -> saw -> square -> narrow pulses
basic = [sine,
         additive([1, 0, 0.15]),                                  # a rounded sine (a hint of 3rd)
         tri,
         additive([1 / n for n in range(1, 7)]),                  # a soft saw (6 harmonics)
         saw,
         sq,
         pulse(0.3),
         pulse(0.12)]

# 2 HARMONIC: organ-like stacks, more partials each wave (1/sqrt(n) so the top stays bright)
harmonic = [additive([1 / np.sqrt(n) for n in range(1, k + 1)]) for k in (1, 2, 3, 5, 8, 13, 21, 34)]

# 3 PULSE: from square to a thin spike
pulses = [pulse(w) for w in (0.5, 0.4, 0.32, 0.25, 0.18, 0.12, 0.07, 0.03)]

# 4 VOCAL: harmonic amplitudes shaped by three formants (as sung at ~110 Hz), oo -> ee
VOWELS = [(300, 870, 2240), (450, 800, 2830), (730, 1090, 2440), (660, 1720, 2410),
          (530, 1840, 2480), (490, 1350, 1690), (390, 1990, 2550), (270, 2290, 3010)]
F0 = 110.0


def vowel(f1, f2, f3):
    amps = []
    for h in range(1, HARM + 1):
        f = h * F0
        g = sum(a * np.exp(-0.5 * ((f - fc) / bw) ** 2) for fc, bw, a in ((f1, 90, 1.0), (f2, 120, 0.6), (f3, 160, 0.35)))
        amps.append(g + 0.02 / h)                                 # a little glottal floor
    return additive(amps)


vocal = [vowel(*v) for v in VOWELS]

# 5 DIGITAL: quantised and stepped shapes
def quant(w, levels):
    return np.round((w + 1) / 2 * (levels - 1)) / (levels - 1) * 2 - 1


def steps(w, n):
    idx = np.floor(x * n) / n
    return np.interp(idx, x, w)


digital = [quant(sine, 16), quant(sine, 8), quant(sine, 5), quant(sine, 3),
           steps(saw, 8), steps(saw, 4), steps(sine, 6), quant(steps(tri, 5), 4)]

# 6 COMPLEX: folded, synced and clustered spectra
def fold(w, gain):
    return np.sin(np.pi / 2 * gain * w)


def sync_saw(ratio):
    return 1 - 2 * ((x * ratio) % 1.0)


rng = np.random.default_rng(1729)                                 # fixed: the same tables every time
cluster = additive([rng.uniform(0.2, 1.0) / n ** 0.3 for n in range(1, 25)], rng.uniform(0, 2 * np.pi, 24))
dense = additive([rng.uniform(0.3, 1.0) for _ in range(HARM)], rng.uniform(0, 2 * np.pi, HARM))
complex_ = [fold(sine, 1.6), fold(sine, 2.6), fold(tri, 4.0), sync_saw(1.5), sync_saw(2.5), sync_saw(3.7),
            cluster, dense]

BANKS = [("BASIC", basic), ("HARM", harmonic), ("PULSE", pulses), ("VOCAL", vocal), ("DIGI", digital),
         ("CMPLX", complex_)]
WAVE_NAMES = [
    ["SINE", "ROUND", "TRI", "SOFTSAW", "SAW", "SQUARE", "PULSE30", "PULSE12"],
    ["H1", "H2", "H3", "H5", "H8", "H13", "H21", "H34"],
    ["PW50", "PW40", "PW32", "PW25", "PW18", "PW12", "PW7", "PW3"],
    ["OO", "OH", "AH", "AE", "EH", "ER", "IH", "EE"],
    ["Q16", "Q8", "Q5", "Q3", "STEP8", "STEP4", "SINE6", "TRI5"],
    ["FOLD1", "FOLD2", "FOLD3", "SYNC1", "SYNC2", "SYNC3", "CLUSTER", "DENSE"],
]

# ---- lookup tables
RATIOS = [0.5, 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
RATIO_NAMES = ["0.5", "1", "1.5", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"]


def env_time(p):
    """parameter 0..127 -> seconds, 1 ms .. 10 s, exponential"""
    return 0.001 * 10000 ** (p / 127)


def blocks(t):
    return max(1.0, t * FS / BLK)


ENV_ATT = [round((1 << 24) / blocks(env_time(p))) for p in range(128)]          # Q24 per block, linear
ENV_EXP = [max(1, round((1 - 0.001 ** (1 / blocks(env_time(p)))) * 65536)) for p in range(128)]   # -60 dB in t
FINE = [round(2 ** (((p - 64) * 50 / 63) / 1200) * 65536) for p in range(128)]   # -50..+50 cents, Q16
SEMI = [round(2 ** ((s - 24) / 12) * 65536) for s in range(49)]                 # -24..+24 semitones, Q16
CURVE = [round((p / 127) ** 2 * 32767) for p in range(128)]                      # amounts, levels: Q15, squared

# ---- the filter (a zero-delay-feedback state-variable low-pass, engine.c)
# cutoff as a pitch in quarter semitones: 0 .. FILT_N-1 = MIDI 0 .. 135.75 (8 Hz .. 20 kHz, held under 0.45 fs)
FILT_N = 544


def filt_hz(q):
    return min(440 * 2 ** ((q / 4 - 69) / 12), 0.45 * FS)


FILT_G = [round(np.tan(np.pi * filt_hz(q) / FS) * (1 << 24)) for q in range(FILT_N)]   # g = tan(pi fc / fs), Q24
# resonance 0..127 -> damping k = 2 (none) .. -0.06, Q24; eased so the top is usable. Past ~122 k is negative: the
# filter self-oscillates (the render limits the band state, so it settles to a sine at the cutoff)
RES_K = [round(max(-0.06, 2 * (1 - (p / 127) ** 0.7) - 0.2 * (p / 127) ** 30) * (1 << 24)) for p in range(128)]
DRIVE = [round(8 ** (p / 127) * 4096) for p in range(128)]                       # drive gain 1 .. 8, Q12

# ---- the LFO
def lfo_hz(p):
    return 0.05 * 800 ** (p / 127)                                              # 0.05 .. 40 Hz


LFO_INC = [round(lfo_hz(p) * BLK / FS * 2 ** 32) for p in range(128)]            # phase step per block
LFO_HZ100 = [round(lfo_hz(p) * 100) for p in range(128)]
PITCH16 = [round(2 ** ((i - 192) / 16 / 12) * 65536) for i in range(385)]         # +-12 semitones, 1/16 steps


def c_array(name, ctype, values, per_line=12):
    lines = [", ".join(str(int(v)) for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
    return f"static const {ctype} {name}[{len(values)}] = {{\n    " + ",\n    ".join(lines) + "};\n"


def main():
    waves = np.array([[band_limit(w) for w in bank] for _, bank in BANKS])
    waves = np.round(waves).astype(np.int16)
    assert waves.shape == (6, 8, N)
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* generated by tools/gen_prysm_tables.py: do not edit */",
           "#ifndef PRYSM_TABLES_H", "#define PRYSM_TABLES_H", "",
           f"#define PW_BANKS {len(BANKS)}u", "#define PW_WAVES 8u", f"#define PW_N {N}u",
           f"#define PR_RATIOS {len(RATIOS)}u", ""]
    out.append("static const char *const PW_BANK_NAME[PW_BANKS] = {" + ", ".join(f'"{n}"' for n, _ in BANKS) + "};")
    out.append("static const char *const PW_WAVE_NAME[PW_BANKS][PW_WAVES] = {")
    out += ["    {" + ", ".join(f'"{n}"' for n in row) + "}," for row in WAVE_NAMES]
    out.append("};")
    out.append("static const char *const PR_RATIO_NAME[PR_RATIOS] = {" + ", ".join(f'"{n}"' for n in RATIO_NAMES) + "};")
    out.append("")
    out.append(f"static const int16_t PW_WAVE[PW_BANKS][PW_WAVES][PW_N] = {{")
    for b, (name, _) in enumerate(BANKS):
        out.append(f"  {{ /* {name} */")
        for w in range(8):
            vals = waves[b, w]
            rows = [", ".join(str(int(v)) for v in vals[i:i + 16]) for i in range(0, N, 16)]
            out.append(f"    {{ /* {WAVE_NAMES[b][w]} */\n      " + ",\n      ".join(rows) + "},")
        out.append("  },")
    out.append("};")
    out.append("")
    out.append("/* ratio to the note, Q8 */")
    out.append(c_array("PR_RATIO_Q8", "uint16_t", [round(r * 256) for r in RATIOS]))
    out.append("/* envelope parameter 0..127 = 1 ms .. 10 s: attack step per 16-frame block (Q24, linear),")
    out.append(" * decay / release coefficient per block (Q16, exponential: -60 dB in that time) */")
    out.append(c_array("PR_ENV_ATT", "uint32_t", ENV_ATT, 8))
    out.append(c_array("PR_ENV_EXP", "uint16_t", ENV_EXP))
    out.append("/* fine 0..127 = -50..+50 cents (64 = 0), semitones 0..48 = -24..+24 (24 = 0): pitch factors, Q16 */")
    out.append(c_array("PR_FINE_Q16", "uint32_t", FINE, 8))
    out.append(c_array("PR_SEMI_Q16", "uint32_t", SEMI, 8))
    out.append("/* 0..127 -> Q15, squared: amounts and levels feel even across the knob */")
    out.append(c_array("PR_CURVE_Q15", "uint16_t", CURVE))
    out.append(f"/* the filter: cutoff index 0..{FILT_N - 1} = MIDI 0..135.75 in quarter semitones (8 Hz .. 0.45 fs):")
    out.append(" * g = tan(pi fc / fs) Q24; resonance 0..127 -> damping k Q24; drive 0..127 -> gain 1..8 Q12 */")
    out.append(f"#define PR_FILT_N {FILT_N}u")
    out.append(c_array("PR_FILT_G", "uint32_t", FILT_G, 8))
    out.append(c_array("PR_RES_K", "int32_t", RES_K, 8))
    out.append(c_array("PR_DRIVE_Q12", "uint16_t", DRIVE))
    out.append("/* the LFO: rate 0..127 = 0.05 .. 40 Hz (exponential): phase step per 16-frame block, and Hz x 100 (shown);")
    out.append(" * pitch modulation: +-12 semitones in 1/16 semitone steps, index 192 = in tune, Q16 */")
    out.append(c_array("PR_LFO_INC", "uint32_t", LFO_INC, 8))
    out.append(c_array("PR_LFO_HZ100", "uint16_t", LFO_HZ100))
    out.append(c_array("PR_PITCH16_Q16", "uint32_t", PITCH16, 8))
    out.append("/* the comb's SPREAD: four allpasses delay f0 by (4 / pi) atan(t0 / tc) of a period (t = tan(pi f / fs)):")
    out.append(" * i = 0..64 for t0 / tc = i / 64, Q15 (32768 = a whole period) */")
    out.append(c_array("PR_ATAN4_Q15", "uint16_t", [round(4 / np.pi * np.arctan(i / 64) * 32768) for i in range(65)]))
    out.append("/* the FILTER page's response curve: 2^(i/24), Q12 (x^2 for i quarter semitones from the cutoff) */")
    out.append(c_array("PR_EXP2_24_Q12", "uint16_t", [round(2 ** (i / 24) * 4096) for i in range(24)]))
    out.append("#endif")
    OUT.write_text("\n".join(out) + "\n")
    print(f"{OUT}: {len(BANKS)} banks x 8 waves, {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    main()
