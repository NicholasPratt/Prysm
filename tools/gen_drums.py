#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate firmware/gen/prysm_drums.h: the drum machine's kits, each five sounds (kick, snare, closed and open hat,
clap), made here from scratch (analog-style models, no recordings). LO-FI is stored as a cheap sampler would: 8 bits,
at a low rate; TR and BOOM in 16 bits, at rates that keep their tops.

  tools/gen_drums.py                    (needs numpy; the header is checked in, rerun after a change here)
  tools/gen_drums.py --wav DIR          also writes each sound as a WAV (as stored), to listen to
  tools/gen_drums.py --kit NAME=DIR     a kit of your own as well (repeatable): DIR's kick.wav, snare.wav, chat.wav,
                                        ohat.wav, clap.wav (PCM, any rate; a missing one: TR's), stored in 16 bits

Use only sounds you have the rights to: a kit of your own goes into the firmware you build, and into anything you
share of it.

The engine plays them back without interpolation (each stored sample held: in LO-FI, the images above the low rate are
the crunch), its CRUSH takes the bits and the rate lower still.

  LO-FI  kick   a sine whose pitch falls fast (165 -> 48 Hz), a click on top, soft-clipped: an 808 with a 909's punch
         snare  two tuned drumhead tones and band-passed noise, each its own decay, driven
         hats   six square waves at the 808's metal frequencies, high-passed: the closed one short, the open one ringing
         clap   four bursts of band-passed noise 9 ms apart, then the room's tail
  TR     a 909's: a tighter kick with more click, a brighter snare, hats of metal and noise, a sharper clap
  BOOM   an 808's: a long sub kick, a low snare, thin hats, a wide clap
"""
import argparse
import wave
from pathlib import Path

import numpy as np

FS = 44100              # made at the engine's rate, then stored at each sound's own
OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "prysm_drums.h"
rng = np.random.default_rng(808)


def t_of(sec):
    return np.arange(int(sec * FS)) / FS


def onepole_lp(x, fc):
    a = 1 - np.exp(-2 * np.pi * fc / FS)
    y = np.empty_like(x)
    s = 0.0
    for i, v in enumerate(x):
        s += a * (v - s)
        y[i] = s
    return y


def onepole_hp(x, fc):
    return x - onepole_lp(x, fc)


def kick():
    t = t_of(0.42)
    f = 48 + 117 * np.exp(-t / 0.032)                       # the pitch drop
    body = np.sin(2 * np.pi * np.cumsum(f) / FS)
    amp = np.where(t < 0.012, 1.0, np.exp(-(t - 0.012) / 0.22))
    click = onepole_hp(rng.uniform(-1, 1, len(t)), 1500) * np.exp(-t / 0.0025) * 0.7
    x = np.tanh(1.8 * (body * amp + click))
    return x * np.minimum(1, (0.42 - t) / 0.03)              # (no click at the end)


def snare():
    t = t_of(0.30)
    tone = (np.sin(2 * np.pi * 182 * t) * 0.65 + np.sin(2 * np.pi * 331 * t) * 0.35) * np.exp(-t / 0.055)
    n = rng.uniform(-1, 1, len(t))
    n = onepole_lp(onepole_hp(n, 900), 7000) * np.exp(-t / 0.095) * 2.2
    x = np.tanh(1.5 * (tone * 0.8 + n))
    return x * np.minimum(1, (0.30 - t) / 0.02)


def hat(sec, decay):
    t = t_of(sec)
    metal = sum(np.sign(np.sin(2 * np.pi * f * 1.6 * t + rng.uniform(0, 2 * np.pi)))
                for f in (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)) / 6
    x = onepole_hp(onepole_hp(metal, 6500), 6500) * 3.0     # (two poles: the metal's top)
    x += onepole_hp(rng.uniform(-1, 1, len(t)), 8000) * 0.25   # (a little air)
    x *= np.exp(-t / decay) * np.minimum(1, t / 0.0005)
    return np.tanh(1.3 * x) * np.minimum(1, (sec - t) / 0.01)


def clap(spread, tail, fc):
    t = t_of(0.08 + 3 * spread + tail * 4)
    n = rng.uniform(-1, 1, len(t))
    n = onepole_lp(onepole_hp(onepole_hp(n, fc * 0.7), fc * 0.7), fc * 1.6)   # (a band around fc)
    env = np.zeros(len(t))
    for i in range(4):                                      # the hands: four bursts, the last the loudest
        t0 = i * spread
        env += np.where(t >= t0, np.exp(-np.maximum(t - t0, 0) / 0.0035), 0) * (0.7 if i < 3 else 1.0)
    t3 = 3 * spread
    env += np.where(t >= t3, np.exp(-np.maximum(t - t3, 0) / tail), 0) * 0.55   # (the room)
    x = np.tanh(2.2 * n * env)
    return x * np.minimum(1, (t[-1] - t) / 0.02)


def kick2(f0, f1, drop, decay, sec, click):
    t = t_of(sec)
    f = f1 + (f0 - f1) * np.exp(-t / drop)
    body = np.sin(2 * np.pi * np.cumsum(f) / FS)
    amp = np.where(t < 0.006, 1.0, np.exp(-(t - 0.006) / decay))
    c = onepole_hp(rng.uniform(-1, 1, len(t)), 2500) * np.exp(-t / 0.0015) * click
    x = np.tanh(1.4 * (body * amp + c))
    return x * np.minimum(1, (sec - t) / 0.04)


def snare2(f0, f1, tone_d, noise_d, hp, sec, snap):
    t = t_of(sec)
    tone = (np.sin(2 * np.pi * f0 * t) * 0.6 + np.sin(2 * np.pi * f1 * t) * 0.4) * np.exp(-t / tone_d)
    n = onepole_lp(onepole_hp(rng.uniform(-1, 1, len(t)), hp), 11000) * np.exp(-t / noise_d) * snap
    x = np.tanh(1.3 * (tone + n))
    return x * np.minimum(1, (sec - t) / 0.02)


def hat2(sec, decay, noise, hp):
    t = t_of(sec)
    metal = sum(np.sign(np.sin(2 * np.pi * f * 1.6 * t + rng.uniform(0, 2 * np.pi)))
                for f in (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)) / 6
    x = onepole_hp(onepole_hp(metal, hp), hp) * 2.4
    x += onepole_hp(onepole_hp(rng.uniform(-1, 1, len(t)), hp), hp) * noise
    x *= np.exp(-t / decay) * np.minimum(1, t / 0.0003)
    return np.tanh(1.2 * x) * np.minimum(1, (sec - t) / 0.01)


def resample(x, src, rate):
    """x at src -> rate: everything above the lower of the two Nyquists removed first (no images, no aliases)"""
    if src == rate:
        return x
    n = len(x)
    f = np.fft.rfft(x)
    f[int(n * 0.45 * min(src, rate) / src * 2 / 2):] = 0
    y = np.fft.irfft(f, n)
    t = np.arange(int(n * rate / src)) * src / rate
    return np.interp(t, np.arange(n), y)


def store16(x, rate, src=FS):
    """x at src -> 16 bits at rate, its peak at full scale"""
    y = resample(x, src, rate)
    y = y / max(np.max(np.abs(y)), 1e-9)
    return np.clip(np.round(y * 32767), -32767, 32767).astype(np.int16)


def wav_read(path, sec=1.5):
    """a PCM WAV (8, 16, 24 or 32 bits; stereo: mixed) as floats at its rate, at most sec long, its silence trimmed"""
    with wave.open(str(path), "rb") as f:
        ch, w, rate, n = f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()
        b = f.readframes(n)
    if w == 1:
        x = np.frombuffer(b, np.uint8).astype(float) - 128
    elif w == 3:
        a = np.frombuffer(b, np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((a[:, 0] | a[:, 1] << 8 | a[:, 2] << 16) << 8 >> 8).astype(float)
    else:
        x = np.frombuffer(b, {2: np.int16, 4: np.int32}[w]).astype(float)
    x = x.reshape(-1, ch).mean(axis=1)
    loud = np.nonzero(np.abs(x) > np.max(np.abs(x)) * 0.002)[0]
    x = x[loud[0]:loud[-1] + 1] if len(loud) else x
    return x[:int(sec * rate)], rate


def store(x, rate):
    """x at FS -> 8 bits at rate: a box low-pass (as a cheap sampler's), then every (FS / rate)th sample"""
    k = FS / rate
    w = max(1, int(round(k)))
    y = np.convolve(x, np.ones(w) / w, mode="same")
    y = y[(np.arange(int(len(x) / k)) * k).astype(int)]
    y = y / np.max(np.abs(y))
    return np.clip(np.round(y * 127), -127, 127).astype(np.int8)


PARTS = ("KICK", "SNARE", "CHAT", "OHAT", "CLAP")          # a kit's sounds, in order (engine.c: DR_KICK_S ..)
MAX_KITS = 8

# (made in this order: the generator's noise is one stream, so LO-FI's first four stay as they were)
LOFI = [(kick(), 11025), (snare(), 16000), (hat(0.075, 0.016), 22050), (hat(0.50, 0.15), 22050)]
LOFI.append((clap(0.009, 0.075, 1200), 16000))
TR = [(kick2(260, 52, 0.022, 0.20, 0.50, 0.9), 22050), (snare2(195, 345, 0.045, 0.11, 1600, 0.34, 2.0), 32000),
      (hat2(0.09, 0.022, 0.5, 7000), 32000), (hat2(0.55, 0.17, 0.5, 7000), 32000), (clap(0.008, 0.06, 1400), 32000)]
BOOM = [(kick2(120, 44, 0.05, 0.42, 0.85, 0.25), 16000), (snare2(150, 240, 0.07, 0.08, 900, 0.30, 1.4), 22050),
        (hat2(0.06, 0.012, 0.2, 8500), 32000), (hat2(0.45, 0.12, 0.2, 8500), 32000), (clap(0.012, 0.11, 1000), 22050)]


def c_array(out, ctype, name, s, per):
    out.append(f"static const {ctype} {name}[{len(s)}] = {{")
    for i in range(0, len(s), per):
        out.append("    " + ", ".join(str(int(v)) for v in s[i:i + per]) + ",")
    out.append("};")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", type=Path, help="also write each sound as a WAV here")
    ap.add_argument("--kit", action="append", default=[], metavar="NAME=DIR",
                    help="a kit of your own: DIR's kick, snare, chat, ohat, clap .wav")
    a = ap.parse_args()
    kits = [("LO-FI", [(store(x, r), r) for x, r in LOFI]), ("TR", [(store16(x, r), r) for x, r in TR]),
            ("BOOM", [(store16(x, r), r) for x, r in BOOM])]
    for k in a.kit:
        name, _, d = k.partition("=")
        if not name or not d:
            ap.error(f"--kit {k}: NAME=DIR")
        sounds = []
        for i, part in enumerate(PARTS):
            f = Path(d) / f"{part.lower()}.wav"
            if f.exists():
                x, r = wav_read(f)
                rate = min(r, 32000)
                sounds.append((store16(x, rate, r), rate))
            else:
                sounds.append(kits[1][1][i])
        kits.append((name.upper()[:6], sounds))
    if len(kits) > MAX_KITS:
        ap.error(f"at most {MAX_KITS} kits")
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* Generated by tools/gen_drums.py: do not edit. The drum machine's kits: LO-FI 8 bits at a low rate, the",
           " * others 16 bits */",
           "#ifndef PRYSM_DRUMS_H", "#define PRYSM_DRUMS_H", "#include <stdint.h>", ""]
    total = 0
    for ki, (kit, sounds) in enumerate(kits):
        for (s, rate), part in zip(sounds, PARTS):
            sym = f"DR_{part}" if ki == 0 else f"DR{ki}_{part}"
            total += s.nbytes
            c_array(out, "int8_t" if s.dtype == np.int8 else "int16_t", sym, s, 24 if s.dtype == np.int8 else 16)
            if a.wav:
                a.wav.mkdir(parents=True, exist_ok=True)
                with wave.open(str(a.wav / f"{kit.lower()}_{part.lower()}.wav"), "wb") as f:
                    f.setnchannels(1)
                    f.setsampwidth(s.itemsize)
                    f.setframerate(rate)
                    f.writeframes((s.astype(np.int16) + 128).astype(np.uint8).tobytes() if s.itemsize == 1
                                  else s.astype("<i2").tobytes())
    out += ["", "/* a sound: its samples (wide: int16_t, else int8_t), how many, its step at 44.1 kHz (Q16: rate / 44100) */",
            "typedef struct { const void *d; uint32_t n, inc_q16; uint8_t wide; } dr_sound_t;",
            f"#define DR_KITS {len(kits)}u",
            "static const char *const DR_KIT_NAME[DR_KITS] = {" + ", ".join(f'"{k}"' for k, _ in kits) + "};",
            f"static const dr_sound_t DR_KIT[DR_KITS][{len(PARTS)}] = {{"]
    for ki, (kit, sounds) in enumerate(kits):
        out.append(f"    {{                                           /* {kit} */")
        for (s, rate), part in zip(sounds, PARTS):
            sym = f"DR_{part}" if ki == 0 else f"DR{ki}_{part}"
            out.append(f"        {{{sym}, {len(s)}u, {round(rate * 65536 / FS)}u, {int(s.dtype != np.int8)}}},"
                       f"   /* {rate} Hz */")
        out.append("    },")
    out += ["};", "#endif", ""]
    OUT.write_text("\n".join(out))
    print(f"{OUT}: {len(kits)} kits, {total} bytes of samples")


if __name__ == "__main__":
    main()
