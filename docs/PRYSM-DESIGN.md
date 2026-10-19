# Prysm: design

A 4-voice, 2-oscillator synth module for the M-VAVE FM-1. Each oscillator is a wavetable carrier modulated by a
series chain of two wavetable modulators, each with its own AR envelope. Then a 2-pole low-pass filter with an
ADSR, and an amp ADSR.

Status: the sound engine, the filter and a first version of the pages are built (host-mode tests: tests/run_engine.sh, screenshots: tests/run_ui.sh). Next: the pages refined on the device.

## Voice

```
 per voice (x4)

 OSC 1:  [mod 2] --mode--> [mod 1] --mode--> [carrier] --+
           AR env            AR env                       |
                                                          +--> mix --> [2-pole LP] --> [amp] --> out
 OSC 2:  [mod 2] --mode--> [mod 1] --mode--> [carrier] --+       drive    ADSR          ADSR
```

- **4 voices**, polyphonic. *(default)* The oldest note is stolen when a fifth arrives.
- **Two oscillators**, identical, each with its own page.
- **Each oscillator is a series chain**: mod 2 acts on mod 1, mod 1 acts on the carrier (as drawn).
- **Each modulator has a mode**, chosen per modulator:
  - **FM**: phase modulation of the wave it points at (DX-style: bright, metallic, bells);
  - **RING**: multiplies it (clangy sidebands; tremolo at low ratios);
  - **MIX**: adds its wave to it (an additive layer).
- **Modulator amount** is the MIX parameter, scaled by the modulator's AR envelope. The envelope is triggered per
  note. *(default)* A holds at full level until the key is released, then R.
- **Pitch**: the carrier and both modulators run at a **ratio to the note**. *(default)* The ratios step
  through 0.5, 1, 1.5, 2, 3, 4 ... 16, and fine detunes the whole oscillator.

## Wavetables

- **Generated in code**, so they're small and entirely ours.
- *(default)* **6 banks of 8 waves**. A **position** control morphs smoothly between neighbouring waves; the
  carrier and the modulators use the same banks.
  1. **BASIC**: sine → triangle → saw → square.
  2. **HARMONIC**: additive, adding partials one by one.
  3. **PULSE**: pulse widths from 50 % to narrow.
  4. **FORMANT**: vowel-like resonances (a → e → i → o → u).
  5. **DIGITAL**: bit-reduced and stepped shapes.
  6. **NOISY / INHARMONIC**: clusters and folded shapes for the "complex" end.

## Filter and amp

- **2-pole state-variable low-pass** (12 dB/oct), per voice.
- **Drive** into the filter (soft saturation).
- **Filter ADSR** with an envelope amount (bipolar, *(default)*) and keytrack.
- **Amp ADSR**, per voice.

## Controls

- **SELECT** moves the row on the current page (a marker shows it).
- **KNOB1–KNOB4** edit the row's (up to) 4 parameters, left to right.
- **Pages** are stepped through with **ALGORITHM**, or reached directly with buttons *(default mapping)*:
  - **EDIT**: OSC 1, press again for OSC 2;
  - **FX**: FILTER;
  - **ENV**: AMP;
  - **GLO**: MIX.
- **MASTER** stays the output volume (platform).
- **OCT- / OCT+** set the keyboard octave (kept).

## Pages

The display is graphical where possible: waves drawn in their boxes and envelopes drawn as shapes, all redrawn as
the knobs turn.

### OSC 1 / OSC 2

```
OSC 1                              1/5
[carrier] <- [ mod 1 ] <- [ mod 2 ]        live wave drawings, arrows between
```

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| WAVE | carrier bank | carrier position | mod 1 bank | mod 1 position |
| WAVE2 | mod 2 bank | mod 2 position | | |
| RATIO | carrier ratio | mod 1 ratio | mod 2 ratio | fine |
| MODE | mod 1 mode | mod 2 mode | | |
| ENV | mod 1 A | mod 1 R | mod 2 A | mod 2 R |
| MIX | mod 1 amount | mod 2 amount | oscillator level | |

### FILTER

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| FILTER | cutoff | resonance | env amount | keytrack |
| ENV | A | D | S | R |
| DRIVE | drive | | | |

The ENV row is drawn as an envelope shape; cutoff/resonance as a response curve *(default)*.

### AMP

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | A | D | S | R |

### MIX

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| OSC | osc 1 level | osc 2 level | osc 2 semitones | osc 2 fine |

## LFO

One LFO per voice (or one for all voices, free-running), on the **LFO** page (the LFO button):

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| LFO | wave: SINE, TRI, SAW UP, SAW DN, SQUARE, S&H | rate 0.05 .. 40 Hz | delay (fade-in, 1 ms .. 10 s) | retrig ON / OFF |
| DEST | destination 1 | amount 1 (bipolar) | destination 2 | amount 2 (bipolar) |

- **Retrig ON**: each note starts its voice's LFO from the beginning (and a new S&H value); **OFF**: one LFO runs
  free and the voices share it.
- **Delay**: the LFO fades in over that time from each note (0 = at once), with retrig on or off.
- **Destinations**: PITCH (both oscillators, +-12 semitones), PITCH2 (osc 2 only), WAVE1 / WAVE2 (the carrier's
  wave position, across the whole bank), MWAVE1 / MWAVE2 (both modulators' wave positions), MOD1 / MOD2 (both
  modulators' amounts), MPITCH (the modulators' pitch: the FM sidebands move), CUTOFF (+-48 semitones), AMP
  (tremolo), MIX (the balance of the two oscillators).
- Wave positions move per voice: each operator's whole bank is copied to RAM and morphed as it plays, only while
  a position destination has an amount.
- The graphic draws the shape (more cycles at faster rates), the delay as a fade-in and the retrig point.

## Patches

The **SAVE** button opens the PATCH screen:

- **8 slots**, drawn as tiles: the slot's number and a thumbnail of osc 1's carrier wave (a dash: empty). The
  selected slot has a double edge; a dot marks the slot the current sound came from.
- **SELECT** picks SAVE, LOAD or INIT; **KNOB1** the slot; **SAVE** again does it. Saving over a used slot (other
  than the current one), and loading or INIT over unsaved changes, ask for a second press.
- **HOME**, ALGORITHM or a page button go back.
- The header shows the current slot and `*` when there are unsaved changes, on every page.
- The slot last saved or loaded is loaded at power-on.
- Stored in two flash sectors written in turn (0xFA000, 0xFB000, `store_load` / `store_save` in platform.c), so
  a save cut by a power loss loses only that save. `PATCH_VERSION` (patches.c) guards the format; older
  formats convert when read (format 1, before the LFO: the LFO comes in with no amount).

## Not in the first version

Velocity, glide, a second LFO, effects, patch names.

## Budget

Per sample, the worst case is 4 voices × 2 oscillators × 3 wavetable reads (each an interpolated morph), plus 4
filters. The audio ISR has 5.8 ms per 256 frames, and the target is under half of that (`docs/RULES.md`).
Envelopes, filter coefficients and morph positions update once per 16-frame block, not per sample. Measure with
`fm1_ticks()` on the device.
