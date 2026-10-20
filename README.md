# Prysm

**A 4-voice wavetable FM synthesizer firmware for the M-VAVE FM-1.**

<p align="center"><img src="docs/screen.png" width="240" alt="Prysm's OSC 1 page: a VOCAL carrier modulated by a CMPLX wave"></p>

Two oscillators per voice, each a chain of three wavetable operators (mod 2 -> mod 1 -> carrier) that modulate
each other by FM, ring or mix; a resonant filter (12 / 24 dB low-pass or a comb) with drive and a high-pass; an LFO with six shapes and two destinations; a second, tempo-syncable LFO with many more; a mod
envelope that can loop and modulate the LFO's rate; stereo chorus, delay (with wow and pitch-shifting repeats) and
a plate reverb (with modulation and shimmer); 8 patch slots; graphical pages; USB audio and MIDI.

- **Install it**: [the web installer](https://nicholaspratt.github.io/Prysm/) (Chrome or Edge), or `tools/fm1_install.py` (below)
- **Use it**: [the manual](docs/MANUAL.md) ([as a page](https://nicholaspratt.github.io/Prysm/manual.html))
- **Build it**: [SETUP-MACOS.md](SETUP-MACOS.md), and below

> Prysm is a community firmware, not made or supported by M-VAVE. Installing firmware is at your own risk. If an
> FM-1 does not start after an update, hold **OCT-** while switching it on: the rescue updater.

## The sound

| | |
| --- | --- |
| **Voices** | 4, polyphonic |
| **Oscillators** | 2 per voice; each a carrier modulated by mod 1, modulated by mod 2. Each operator: 6 banks x 8 morphing waves (BASIC, HARM, PULSE, VOCAL, DIGI, CMPLX), a ratio to the note (0.5 .. 16); each modulator: FM / RING / MIX, an amount, its own AR envelope |
| **Filter** | state-variable low-pass, 12 or 24 dB/oct (zero-delay feedback), or a Blofeld-style comb (COMB+ / COMB-: the cutoff its frequency, resonance its feedback, plus damping and spread); drive, keytrack, its own ADSR; a 12 dB/oct high-pass (no resonance) |
| **Amp** | ADSR |
| **LFO** | sine, triangle, saw up / down, square, S&H; 0.05 .. 40 Hz; delay; retrig or free-running; two destinations (pitch, wave positions, mod amounts, the modulators' pitch, cutoff, amp, mix) |
| **LFO 2** | one for all the voices; the same shapes; free (0.05 .. 40 Hz) or in time (4/1 .. 1/32, triplets); any LFO destination, or resonance, drive, high-pass, comb damping, filter envelope amount, levels, osc 2 detune, the mod envelope's amount, the effects' mixes, delay time and feedback, reverb size |
| **Mod envelope** | ADSR with LOOP (attack-decay until release); into the LFO's rate or depth, or any LFO destination |
| **Effects** | stereo chorus; delay up to 735 ms with ping-pong, tone, wow and per-repeat pitch shift; Dattorro plate reverb with pre-delay, damping, modulation and shimmer |
| **Velocity** | from MIDI, to level, cutoff and modulation (the FM-1's keys play at full velocity) |
| **Glide** | portamento, always or legato |
| **Arpeggiator** | UP, DOWN, UPDN, RAND, ORDER; 1/4 .. 1/32 with triplets; 1 .. 4 octaves; gate (to tied), swing, latch; 40 .. 240 BPM or the MIDI clock (START / STOP / CONTINUE); sample-accurate |
| **Sequencer** | up to 64 steps, 303-style: notes, rests, accents, slides (legato glide, no new attack); step entry from the keys (REC); PLAY runs it; its own or the MIDI clock |
| **Oscilloscope** | on HOME: left and right, locked to the newest note, auto-zoom |
| **Patches** | 8 slots, saved in flash (power-cut safe), the last one back at power-on |
| **USB** | class-compliant stereo audio input ("Prysm FM-1", 44.1 / 48 kHz, works on macOS 14) and MIDI in / out, with channel select and clock |

Everything runs in fixed point on the FM-1's JieLi AC791N (no FPU); the header shows how much of the audio budget
the sound takes.

## The pages

ALGORITHM steps through the pages (or a button jumps to one), SELECT picks a row, KNOB1..4 edit its four values;
PRESETS is the filter's cutoff on every page.
Every page draws what it does: the oscillator chain with its live waves, the filter's response curve, the
envelopes, the LFO, the effects chain.

| Page | Button | Rows |
| --- | --- | --- |
| OSC 1, OSC 2 | EDIT | WAVE, WAVE2, RATIO, MODE, ENV, MIX |
| FILTER | ENV (twice) | FILTER, ENV, TYPE, COMB |
| AMP | ENV | ENV, VEL |
| LFO | LFO (twice: LFO 2) | LFO, DEST, M.ENV, M.DST; LFO 2: LFO, DEST |
| MIX | GLO | OSC, GLIDE |
| SETUP | GLO (twice) | MIDI: IN / OUT channels, CLOCK, pitch BEND range; SCREEN: THEME |
| ARP | ARP (again: on / off) | ARP, TIME |
| SEQ | SEQ (PLAY: run, REC: step entry) | STEP, PATTERN |
| CHORUS / DELAY / REVERB | FX (1, 2, 3 times) | CHORUS; DELAY, D.MOD; REVERB, R.MOD |
| PATCH | SAVE | SAVE, LOAD, INIT over 8 slots |
| SCOPE | HOME | the output, live (KNOB1: cycles) |

## Installing from the command line

```sh
pip3 install mido python-rtmidi
python3 tools/fm1_install.py prysm-0.6.fwsc       # asks before it writes; restarts into Prysm
python3 tools/fm1_install.py --info               # what the connected FM-1 runs
```

From ETCH: restore the official firmware first (ETCH only accepts its own updates). Back to the official firmware
or another one: the [FM-1 Firmware Hub](https://thefirmwarehub.com).

## Building

The toolchain is JieLi's (Linux; on a Mac it runs in Docker). [SETUP-MACOS.md](SETUP-MACOS.md) has the details;
`tools/setup_macos.sh` sets it all up.

```sh
tools/setup_macos.sh                 # once: colima + docker, the JieLi toolchain, the AC79 SDK, a Python venv
export PATH="$PWD/.venv/bin:$PATH"
./build.sh                           # build/prysm.fwsc (identity FM-1_980)
./build.sh --release 0.6             # build/prysm-0.6.fwsc (identity FM-1_986, "0.6" on the splash)
tools/make_site.py build/prysm-0.6.fwsc 0.4   # site/: the web installer and the manual (GitHub Pages)
```

### Tests (on the computer, no FM-1 needed)

```sh
tests/run_engine.sh     # renders test sounds and checks them by spectrum: oscillators, filter, LFO, mod envelope,
                        # effects; and the patch bank's format conversions
tests/run_ui.sh         # screenshots of every page and of a save / load session: build/ui/*.png
tests/run_host.sh       # the whole app on the computer: build/host/prysm.wav and screens
```

### Where things are

| | |
| --- | --- |
| `firmware/src/engine.c` | the voices: oscillators, modulators, filter, envelopes, LFO, mod envelope |
| `firmware/src/fx.c` | chorus, delay, reverb |
| `firmware/src/ui.c` | the pages and the PATCH screen |
| `firmware/src/patches.c` | the 8 slots and their format conversions |
| `firmware/src/prysm.c` | the app: notes, the splash, the main loop |
| `firmware/src/platform.c`, `usb.c`, `hal/`, `loader/` | the platform (Dinghy / Felucca / SLOOP), with Prysm's USB audio layout and store |
| `tools/gen_prysm_tables.py`, `tools/gen_splash.py` | the wavetables and lookup tables; the splash picture |
| `web/`, `tools/make_site.py` | the web installer |
| `docs/` | the [manual](docs/MANUAL.md), the [design](docs/PRYSM-DESIGN.md), the platform's [API](docs/API.md) and [rules](docs/RULES.md) |

## Credits and licence

Prysm by Nicholas Pratt. It is built on [Dinghy](https://github.com/zvenson/dinghy) (zvenson), which carries the
platform of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), as
[SLOOP](https://github.com/isod89/sloop-fm1) (isod89) carries it: the boot, the updater, the USB stack, the
screen and the flash driver are theirs. The reverb is Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997). The
web installer's update protocol is Felucca's `web/fm1ota.js` and `web/fm1pkg.js`.

GPL-3.0 only: see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md). If you give someone a firmware built from
it, give them its source too. The package holds three JieLi AC79 SDK files (Apache-2.0, `LICENSES/`).

M-VAVE and FM-1 are trademarks of their respective owners.
