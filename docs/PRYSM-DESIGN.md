# Prysm: design

A 4-voice, 2-oscillator synth module for the M-VAVE FM-1. Each oscillator is a wavetable carrier modulated by a
series chain of two wavetable modulators, each with its own AR envelope. Then a 2-pole low-pass filter with an
ADSR, and an amp ADSR.

Status: release 0.2 (site/: the web installer and the manual). User manual: docs/MANUAL.md.

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

- **SELECT** moves the row on the current page (the tabs along the bottom show it).
- **KNOB1–KNOB4** edit the row's (up to) 4 parameters, left to right.
- **Pages** are stepped through with **ALGORITHM**, or reached directly with buttons:
  - **EDIT**: OSC 1, press again for OSC 2;
  - **ENV**: AMP, press again for FILTER;
  - **LFO**: LFO, press again for LFO 2;
  - **GLO**: MIX, press again for SETUP;
  - **FX**: CHORUS, then DELAY, then REVERB;
  - **SELECT** (the button): FILTER.
- **PRESETS** is the filter's cutoff on every page but PATCH (shown large in the picture's corner for 1.2 s).
- **MASTER** stays the output volume (platform).
- **OCT- / OCT+** set the keyboard octave (kept).

## Pages

The display is graphical where possible: waves drawn in their boxes and envelopes drawn as shapes, all redrawn as
the knobs turn. White and greys on black. Top to bottom (ui.c): the header (page > row; slot, `*`, a CPU bar), the
picture (240 x 124: a faint dotted grid, one line weight, a dim fill under curves, what the row touches in white),
the focus row (the selected row's four labels, values in FONT_M, Terminus 22 bold from tools/gen_prysm_font.py, and a
bar under each level), the tabs (the page's rows, the selected one inverted). A knob turned: its name and value in
FONT_P (Terminus 32 bold) in a box in the picture's top right corner for 1.2 s. The PATCH screen
has the same header and tabs (SAVE, LOAD, INIT), the slots as tiles in the picture.

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
| TYPE | LP 12, LP 24, COMB+, COMB- | drive | high-pass cutoff (0 = off) | high-pass resonance |
| COMB | damping | spread | | |

The ENV row is drawn as an envelope shape; cutoff/resonance as a response curve *(default)* (LP 24: times its first
stage's; the comb: in^2 / (1 + fb^2 - 2 fb cos(2 pi f / f0)), damping and spread left out), with the high-pass's slope.

Per voice: drive -> the filter (TYPE) -> high-pass -> amp (patch format 8: `hp_cut`, `flt_type`, `comb_damp`,
`comb_spread`). **LP 24** puts a fixed Butterworth SVF (k = sqrt 2, its own coefficients glided the same way) before
the resonant one. The **high-pass** is a zero-delay-feedback SVF (12 dB / octave), 16 Hz .. 4.2 kHz; HP RES (patch format 15: `hp_res`)
sets its damping k from 2 (no peak: two one-poles' response, as before) down to 0.16 (+16 dB at the cutoff, soft-clipped).
The **comb** (the Blofeld's Comb+/-) replaces the low-pass and takes its knobs: its frequency = MIDI 12 + 0.75 x CUT
(so 64 is C4 and 100 % keytrack is in tune) + the cutoff's own modulation (envelope, keytrack, velocity, LFO / mod
envelope CUTOFF) + keytrack x (glide and LFO pitch); RES -> feedback 0.98 (1 - (1 - r)^2), negated for COMB-. Its
delay is one period (interpolated, Q8 frames), a one-pole damping in the loop, the input scaled by sqrt(1 - |fb|):
broadband about -3 dB whatever the feedback, the peaks up to +17 dB, a cubic soft clip in the loop rounding them off.
**SPREAD** (also an LFO / mod envelope destination) puts four
first-order allpasses in the loop, their corner 60 .. 10 semitones above the comb's frequency: the peaks above the
corner see less delay, so they stretch apart; the line is shortened by the allpasses' delay at the frequency itself,
(4 / pi) atan(t0 / tc) of a period (`PR_ATAN4_Q15`, exact for the bilinear allpass), so the first peak stays in tune. Its lines are 4 x 2048 frames (down to 21.5 Hz) in RAM, not the pool (16 KB; .bss is then
~64 of 96 KB); cleared when it is turned on or a voice starts from silence.

### AMP

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | A | D | S | R |
| VEL | velocity to level | to cutoff | to modulation | |

Velocity comes from MIDI only (the FM-1's keys are on / off: they play at 127, where velocity changes nothing):
level by the square of velocity, cutoff down to 36 semitones, both mod amounts. Patch format 5.

### ARP (the ARP button; PLAY: on / off)

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ARP | on / off | UP, DOWN, UPDN, RAND, ORDER | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1 .. 4 octaves |
| TIME | 40 .. 240 BPM | gate 5 .. 100 % (127: tied) | swing 50 .. 75 % | latch |

The arp runs in the audio ISR (`arp_run`, before the voices): it counts frames block by block and starts / stops its
notes at the exact 16-frame block (`itrig` / `ioff` per voice), so its timing does not depend on the main loop.
The held notes come from the main loop (`arp_key_down` / `_up`, the keys and MIDI while it is on) through a
sequence-counted copy. Patch format 6. Glide (MIX page): each new note slides from the last one's pitch,
exponentially, ALWAYS or LEGATO (only while another note is held); arp notes too.

**MIDI clock** (SETUP: CLOCK = MIDI): usb.c calls `midi_rt_hook` as each clock / transport byte arrives (in the
USB poll, TIMER5); prysm.c's `midi_rt` turns the 24 MHz timer into an audio frame (the last render's start plus the
time since, x 147 / 80000) and queues it (`engine_clock`). `arp_run` plays each event one render later at its own
block: a constant 5.8 ms latency and no jitter. A step every 24 / steps-a-beat ticks; START from the top, STOP,
CONTINUE; the tick length (smoothed; pauses left out) gives the gates, the swing and the BPM shown.

### SEQ (the SEQ button; PLAY: run / stop; REC: step entry)

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| STEP | the cursor (1 .. 64) | its note | REST / NOTE / ACCENT | slide |
| PATTERN | length 1 .. 64 | 1/4 .. 1/32 | gate | tempo (the arp's) |

Up to 64 steps per patch (`seq_note`, `seq_trig`, `seq_slide`: patch format 7). It runs in the ISR beside the arp,
on the same clock (its tempo, swing, the MIDI clock; START from step 1), one voice, block-accurate. ACCENT = velocity
127 against 100 (AMP's VEL row shapes it). SLIDE holds the note into the next step and moves its pitch there
(`islide`: no voice_start, a ~30 ms glide of its own, `voice.gk`). Step entry: with REC on the SEQ page, a key or a
MIDI note writes its note at the cursor and moves it on. The page draws 64 squares (16 a row).

### SETUP (GLO twice)

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| MIDI | in: OMNI, 1 .. 16 | out: 1 .. 16 | the arp's clock: INT / MIDI | pitch bend range: OFF, +-1 .. 24 st |

Global, not per patch: `gset` (ui.c), saved in the settings sector with the octave and the last slot (an older
record still loads: 5 bytes, before BEND, which then comes in at +-2; or 2 bytes). MIDI pitch bend (0xE0, any
channel the IN setting lets through) -> `engine_bend`: +-range semitones on both oscillators (as the LFO's PITCH, so
the comb's keytrack follows).

### SCOPE (HOME)

The output before MASTER, left (white) and right (grey), captured by the ISR only while shown (2048 frames);
triggered on a rising zero over 1 .. 8 cycles (KNOB1) of the newest note's period; auto-zoom x1 .. x8; redrawn
every 40 ms.

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
| M.ENV | mod envelope A | D | S | R |
| M.DST | LOOP ON / OFF | destination | amount (bipolar) | |

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

### The mod envelope

An ADSR per voice (rows M.ENV, M.DST), started by each note:

- **LOOP ON**: after the decay it attacks again, and again, until the key is let go (then the release): it cycles
  between full and the sustain level (sustain 0: full sweeps).
- **Destinations**: **LFORATE** (the LFO's rate, +-4 octaves at full amount: an envelope that speeds the LFO up
  and down), **LFOAMT** (the LFO's depth, x 0 .. 2), or any of the LFO's own (pitch, waves, mod amounts, cutoff,
  amp, mix). With retrig off, the shared LFO follows the newest note's envelope.
- The graphic, on these two rows: 2 s with the key held 1.5 s, simulated with the engine's own steps: the
  envelope, and with LFORATE / LFOAMT the LFO under it as the envelope moves it.

### LFO 2

Its own page, **LFO 2** (the LFO button twice; ALGORITHM: after LFO): row LFO (wave, rate 0.05 .. 40 Hz, sync
OFF / 4/1 .. 1/32 with triplets), row DEST (destination, amount). One for all the voices, free-running (patch format 9: `lfo2_*`), computed once a block before the voices (`g2[]`,
like the shared LFO's phases):

- **SYNC**: a cycle is `L2SYNC_TICKS` MIDI-clock ticks (24 a beat) of the arp's tempo, or the MIDI clock's period
  when the arp follows one; the phase step a block is 2^32 / (blocks a cycle), worked out each render (32-bit only:
  2^32 / Q8 blocks, << 8). It restarts when the sequencer or the MIDI clock starts.
- **Destinations**: the LFO's (through `mod_add`, the same value for every voice), plus `DEST_RES` ..
  `DEST_REVSIZE` (after `DEST_N`, so the LFO's and the mod envelope's lists stay as they were): each moves its knob
  by up to +-127 (`knob_moved`) and the ISR derives the value from it as engine_set would (`hp_g_of`, `comb_of`,
  `damp_of`, `drive_of`; the effects' in fx_render, block by block, from the knobs fx_set keeps in `fxp`). FENV adds
  to the envelope's depth (+-60 st), DETUNE to osc 2's pitch (+-1 st), MENVAMT to the mod envelope's amount.
- An effect with its mix at 0 is still run when LFO 2 is on its mix (CHORUS, DELAY, REVERB).

## Effects

After the voices, in stereo: **chorus -> delay -> reverb** (`firmware/src/fx.c`), saved with each patch. Each is
off while its mix is 0 (the dry sound passes untouched, at no cost). A page each, **CHORUS** (CHORUS),
**DELAY** (DELAY, D.MOD), **REVERB** (REVERB, R.MOD), the FX button stepping through them; the same picture of the
chain on each, its own effect lit:

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| CHORUS | rate | depth | mix (full = half wet) | |
| DELAY | time, 10 .. 735 ms | feedback, up to 0.95 | mix | ping-pong on / off |
| D.MOD | wow rate | wow depth (the time swept, up to +-3 ms) | shift: each repeat -12 .. +12 st | tone (the repeats darken) |
| REVERB | size (decay) | damping | mix | pre-delay, up to 93 ms |
| R.MOD | rate | depth (the tank's allpasses swept) | shimmer | |

- **Chorus**: a 7 ms line swept by a sine, left and right a quarter cycle apart.
- **Delay**: two 743 ms lines; the feedback goes through the tone low-pass and, with SHIFT, a two-head pitch
  shifter, so each repeat steps up or down (the first repeat is the note as played). Ping-pong: in on the left,
  then across and back.
- **Reverb**: Dattorro's plate (JAES 1997) scaled to 44.1 kHz: pre-delay, four input diffusers, a two-sided tank
  with damping; its first allpasses are swept (the tail's wobble). **Shimmer** crossfades one side's feed with
  itself an octave up, inside the tank: the tail climbs, and the loop gain stays the decay's (< 1), so it never
  runs away.
- The graphic: the chain as three boxes, a mix bar under each; the chorus's two swept lines, the delay's repeats
  (spaced by the time, fading by the feedback, alternating for ping-pong, stepping with the shift), the reverb's
  pre-delay and tail.
- Memory: ~230 KB of lines in the pool (the pool is then 315 of 336 KB). CPU, measured on the FM-1 (0.2): 4 notes
  held with every effect on peaks at **44 %** of the audio budget.

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
  formats convert when read (format 1, before the LFO: the LFO comes in with no amount; format 2, before the
  effects: they come in off; format 3, before the mod envelope: it comes in with no amount; format 4, before velocity: it comes in as the
  default sound has it; format 5, before glide and the arp: off; format 6, before the sequencer: an empty
  16-step pattern; format 7, before the high-pass and the comb: off, LP 12; format 8, before LFO 2: no
  destination).

## Not in the first version

MIDI clock out, song mode (chained patterns), patch names.

## Budget

Per sample, the worst case is 4 voices × 2 oscillators × 3 wavetable reads (each an interpolated morph), plus 4
filters. The audio ISR has 5.8 ms per 256 frames, and the target is under half of that (`docs/RULES.md`).
Envelopes, filter coefficients and morph positions update once per 16-frame block, not per sample. Measure with
`fm1_ticks()` on the device.
