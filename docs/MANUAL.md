# Prysm manual

Prysm is a 4-voice synthesizer firmware for the **M-VAVE FM-1**. Each voice has two oscillators, and each oscillator is a
little modulation chain of three wavetable operators, and a noise source (six kinds) beside them; each of the three
goes through a resonant filter (12 or 24 dB low-pass, or a comb) or straight past it. Then a high-pass, an amp envelope, an
LFO, two looping mod envelopes, a mod matrix, a phaser / flanger, a bit-crusher and stereo chorus, delay and reverb.
Alongside it a **drum machine** (kick, snare, hat, clap; three kits) plays a lane of its own in every sequencer pattern,
the sequencer's steps can lock knobs, and a **looper** records what you play over it.

- [Installing](#installing)
- [Playing](#playing)
- [Finding your way around](#finding-your-way-around)
- [The pages](#the-pages): [OSC 1 / OSC 2](#osc-1--osc-2), [NOISE](#noise), [FILTER](#filter), [AMP](#amp), [LFO](#lfo),
  [LFO 2](#lfo-2), [MATRIX](#matrix), [MIX](#mix), [ARP](#arp), [SEQ](#seq), [DRUM](#drum), [LOOP](#loop),
  [FX](#fx) (CHORUS, DELAY, REVERB, MOD FX, CRUSH), [SETUP](#setup)
- [Patches](#patches)
- [The oscilloscope](#the-oscilloscope)
- [USB: audio and MIDI](#usb-audio-and-midi)
- [The editor](#the-editor)
- [If something goes wrong](#if-something-goes-wrong)
- [Sound ideas](#sound-ideas)

---

## Installing

**From the web installer** (Chrome or Edge, on a computer): open the Prysm installer page, connect the FM-1 by USB
straight to the computer, press **Install Prysm**, allow MIDI access when the browser asks, and wait for the FM-1 to
restart. Do not unplug it while it writes.

**From the command line** (a built package, see the README):

```sh
python3 tools/fm1_install.py prysm-1.3.fwsc
```

**Coming from another firmware.** Most community firmwares (Felucca, SLOOP, Dinghy and their family, and the official
M-VAVE firmware) accept Prysm directly. **ETCH** only accepts its own updates and the official firmware: restore the
official V15 first (ETCH's own installer has *RESTORE - OFFICIAL V15*), then install Prysm.

**Going back.** The [FM-1 Firmware Hub](https://thefirmwarehub.com) installs the official firmware and every
community firmware; Prysm answers its updater like the others.

Prysm is a community firmware, not made or supported by M-VAVE. Installing it is at your own risk.

---

## Playing

When the FM-1 starts, rainbow static collapses into the word PRYSM (press any key or button to skip it), then the
**OSC 1** page. The sound is the last patch you saved or loaded.

| | |
| --- | --- |
| **Keys** | play; 4 voices. The key LEDs show what sounds. The notes also go out over USB-MIDI. |
| **OCT- / OCT+** | the keyboard's octave (-2 .. +2; remembered when the FM-1 is switched off) |
| **MASTER** | the volume |
| **MIDI in** | over USB or the FM-1's TRS MIDI IN jack, alike: notes play Prysm with their velocity (see AMP's VEL row), on any channel or the one set on SETUP; the MIDI clock and START / STOP from either |

When a fifth note comes, the voice that is quietest (or, if all are held, the oldest) gives way.

---

## Finding your way around

Every page works the same way:

```
 OSC 1 > WAVE              3* [###   ]     <- page > row          slot, unsaved (*), CPU bar
 ----------------------------------------
  CAR       MOD1      MOD2
 [carrier] <- [ mod 1 ] <- [ mod 2 ]       <- the page's picture: what the row touches is white
 ----------------------------------------
  BANK     WAVE     BANK     WAVE          <- the selected row: its four knobs' names,
  BASIC    SINE     BASIC    SINE          <- their values, large,
  ---      ----                            <- and a bar under each level
 ----------------------------------------
  [WAVE]  WAVE2  RATIO  MODE  ENV          <- the page's rows: SELECT moves along them
```

Turning a knob shows its name and value large in the picture's top right corner for a moment (PRESETS too: the
cutoff).

| Control | Does |
| --- | --- |
| **ALGORITHM** | the next / previous page |
| **PRESETS** | the filter's cutoff, from any page (shown large for a moment) |
| **SELECT** | the row (the tabs along the bottom) |
| **KNOB1 .. KNOB4** | the row's four values, left to right. Turn fast to move quickly. Modes, LFO waves and on/off switches step every two clicks. |
| **EDIT** | OSC 1 (press again: OSC 2, then NOISE) |
| **ENV** | AMP (press again: F.ENV, then M.ENV 1, then M.ENV 2) |
| **SELECT** (the button) | FILTER |
| **LFO** | LFO (press again: LFO 2, then MATRIX) |
| **GLO** | MIX (press again: SETUP) |
| **ARP** | ARP (on the ARP page: the arpeggiator on / off; its LED shows it) |
| **SEQ** | SEQ (press again: DRUM, then LOOP) |
| **PLAY** | the sequencer: run / stop (from any page; its LED shows it) |
| **REC** | step entry on the SEQ page; while the sequencer plays, real-time recording (on the DRUM page: the drums; on the LOOP page: the looper) (its LED shows it) |
| **FX** | CHORUS (press again: DELAY, REVERB, MOD FX, then CRUSH) |
| **HOME** | the oscilloscope (press again: LIVE) |
| **SAVE** | the PATCH screen |

The header's battery shows **0 to 3 bars**; empty, it blinks: charge the FM-1 soon. Connected to a computer it shows a
**bolt** (USB power) instead, as the FM-1 cannot tell its charge while it is charging. The splash shows it too, and the
SETUP page in words, with the raw reading (ADC).

The header's **CPU** bar is how much of the FM-1's time the sound takes (its peak over the last half second); it
turns bright past 70 %. Keep it under that: near 100 % the sound starts to crackle.

Values go from 0 to 127 unless the page says otherwise. Bipolar values (shown with a sign, like `+24`) are 0 in the
middle of their knob.

---

## The pages

### OSC 1 / OSC 2

Each oscillator is a chain: **mod 2 -> mod 1 -> carrier**. The carrier is what you hear; mod 1 changes the carrier,
and mod 2 changes mod 1. The picture draws the three as boxes with the waves they are playing, and the arrows
between them: an arrow gets thicker with more modulation, and the symbol on it is its mode
(**~** FM, **x** ring, **+** mix).

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| WAVE | carrier bank | carrier wave | mod 1 bank | mod 1 wave |
| WAVE2 | mod 2 bank | mod 2 wave | | |
| RATIO | carrier ratio | mod 1 ratio (PIT1 in MIX / RING) | mod 2 ratio (PIT2 in MIX / RING) | fine tune |
| MODE | mod 1's mode | mod 2's mode | mod 1 fine (FINE1) | mod 2 fine (FINE2) |
| ENV | mod 1 attack | mod 1 release | mod 2 attack | mod 2 release |
| MIX | mod 1 amount | mod 2 amount | the oscillator's level | |

**Waves.** Six banks of eight waves, from simple to complex. The **wave** knob moves smoothly through the bank,
morphing from one wave into the next (the name shown is the nearest).

| Bank | Its waves |
| --- | --- |
| BASIC | sine, rounded, triangle, soft saw, saw, square, pulse 30 %, pulse 12 % |
| HARM | organ-like stacks of 1, 2, 3, 5, 8, 13, 21, 34 harmonics |
| PULSE | pulse widths from square (50 %) to a thin 3 % |
| VOCAL | vowels: oo, oh, ah, ae, eh, er, ih, ee |
| DIGI | stepped and bit-reduced shapes |
| CMPLX | folded sines, hard-synced saws, harmonic clusters |

**Ratio.** Each of the three runs at a ratio to the note: 0.5, 1, 1.5, 2, 3 ... 16. With FM, whole-number ratios
give harmonic, instrument-like tones; 1.5 and 0.5 give hollow and sub tones. **Fine** detunes the whole
oscillator by up to +-50 cents.

**Modes.** How a modulator acts on the wave it points at:

- **FM** (phase modulation): bright, metallic, bell-like, the DX7 sound. More amount, more harmonics.
- **RING**: multiplies; at full amount you hear the sum and difference of the two pitches, not the carrier itself.
- **MIX**: adds the modulator's wave to the other one, a layer.

**Pitch in MIX and RING.** A modulator in MIX or RING is heard, not just felt, so a ratio is the wrong tool: its
RATIO knob becomes **PIT1** (or **PIT2**), the modulator's pitch against the carrier, -24 .. +24 semitones. Switch
back to FM and the ratio is there again, as you left it (each mode keeps its own setting). **FINE1** and **FINE2**
(MODE row) detune each modulator by up to +-50 cents, in any mode: in MIX a chorused layer, in FM a slowly beating,
inharmonic shimmer. The pitch is a modulation destination too (**M1PITCH**, **M2PITCH**, below).

**Envelopes.** Each modulator has its own attack and release (1 ms .. 10 s): the amount rises over the attack,
holds while the key is down, and falls over the release. A slow mod 1 attack on FM makes a note that brightens as
you hold it.

**MIX row.** The modulators' amounts (how much they change the next one) and the oscillator's own level.

### NOISE

A noise source in every voice, beside the two oscillators: it follows the amp envelope like them, and goes through the
filter or past it (ROUTE). **EDIT** three times. The picture is the noise itself: its wave (a few milliseconds) above,
its spectrum below (24 bands, 60 Hz .. 15 kHz, the loudest at the top), so you see its colour change as you turn.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| NOISE | TYPE | COLOUR (or RATE, DENSE) | LEVEL (0: off) | ROUTE: FILTER / DIRECT |

- **WHITE**: flat hiss. **PINK**: softer (-3 dB an octave), wind and surf. **BROWN**: darker still (-6 dB an
  octave), a rumble. For these three, **COLOUR** is a tone (127 open, down: darker).
- **DIGI**: a game console's noise channel, a short-loop shift register: buzzy, metallic and **pitched to the key**
  (play it like an oscillator). **RATE** moves it +-48 semitones from the note.
- **S&H**: random values held for a step, the step 16 times the note's: a crunchy, bit-crushed hiss that follows the
  keys. **RATE** as DIGI's.
- **CRACKLE**: sparse clicks like an old record. **DENSE**: how many, about 2 .. 2000 a second.

The noise's level is on the MIX page too (N.LVL), beside the oscillators'.

### FILTER

A filter for each voice, with drive before it, a high-pass after it, and its own envelope (on the **F.ENV** page:
ENV, twice). TYPE picks what it is: a
**LP 12** (2-pole, 12 dB / octave) or **LP 24** (4-pole, 24 dB / octave) low-pass, or a comb, **COMB+** or **COMB-**.
The picture draws the filter's actual response (the low-pass's curve or the comb's peaks, and the high-pass) and the
envelope's shape.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| FILTER | cutoff (16 Hz .. 20 kHz) | resonance | envelope amount (bipolar, +-60 semitones) | keytrack (0 .. 100 %) |
| TYPE | LP 12, LP 24, COMB+, COMB- | drive (0 = clean) | high-pass cutoff (OFF, 16 Hz .. 4.2 kHz) | HP RES: the high-pass's resonance (0 = none; up: a peak at its cutoff, to +16 dB, soft-clipped when loud) |
| COMB | damping (0 bright .. 127 dark) | spread | | |

Cutoff and resonance glide as you turn them, so sweeps are smooth. **Keytrack** at 100 % moves the cutoff with the
note, so every key keeps the same brightness. A negative envelope amount closes the filter as the envelope rises.

**LP 24** is steeper and darker than LP 12; its resonance is a little softer.

**Self-oscillation**: from RES **122** up, the low-pass sings on its own: a sine at the cutoff, held at a steady level
from the lowest cutoffs to the highest, even with the oscillators' levels at 0. With KEY at 127 it follows the keys
in tune (an octave up the keyboard, an octave up in pitch); CUT sets where: at about **47** each key sings near its
own note (a little sharp: tune by ear), each step of CUT about a semitone higher. The amp envelope still shapes it. Turn RES back below 122 and it rings
and dies away as before.

**COMB+ / COMB-** turn the filter into a comb, as on a Waldorf: a row of resonant peaks at a frequency and every
multiple of it. The FILTER row then plays the comb: CUT sets the first peak (in 3/4-semitone steps: 64 is C4, 80 is
C5), RES its feedback (from no effect to long ringing), ENV sweeps it with the envelope and KEY tracks the keyboard.
With KEY at 127 (100 %) and CUT at 64, the peaks sit on each note's own harmonics: a plucked string from a short
burst, with a long amp release. COMB+ rings on the peaks, metallic and plucked; COMB- puts them halfway between,
hollow and an octave down. On the **COMB** row, DAMP darkens the ringing and **SPREAD** stretches the upper peaks
further and further apart while the first stays put: from an even comb to bells, bars and metal. The peaks get
louder as RES rises (up to about +17 dB, rounded off rather than clipped), so the comb keeps its level even when they
miss the harmonics. High feedback takes a moment to build up on low notes.

The LFO and the mod envelopes move the comb through **CUTOFF**, and its spread through **SPREAD**.

### AMP

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | attack | decay | sustain | release |
| VEL | velocity to level | velocity to cutoff | velocity to modulation | |
| VCA | OPEN: the amp held open this much (0 = closed) | | | |

Times go from 1 ms to 10 s.

**OPEN** (VCA row) holds the amp open whatever the envelope does, as a VCA bias on an analog synth (the Neutron's,
say): an instant **drone**. Turn it up and the oscillators sound at once, at the pitch they were last played at (C3
when nothing has been played since power-on), the whole sound: FM, filter and all. While a voice drones, its other
envelopes (the modulators', the filter's, the mod envelopes) hold as if its key were down, so every knob works on it
live: turn AMT1 and the drone brightens. Keys play over it with their envelope (from the drone's level up), and the
notes you let go drone on, so a chord let go becomes a drone chord. The picture draws the level as a line across the
envelope. Turn OPEN back to 0 and the drone releases and fades out. (A drone keeps up to four voices running: watch the CPU bar with every effect on.)

**Velocity** comes from MIDI: a keyboard or a DAW playing Prysm over USB. The FM-1's own keys have no velocity (they
are on / off), so they always play at full velocity and these settings do not change them. Each sensitivity says how
much a softer note is quieter (by the square of its velocity, as it sounds natural), darker (up to 36 semitones of
cutoff) or less modulated (less FM brightness: the classic expressive FM touch). At 0, velocity changes nothing.

### F.ENV

The filter's envelope (ENV twice, or ENV from AMP). Its amount, how far it moves the cutoff, is the FILTER page's
ENV knob; the picture shows it in its corner.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | attack | decay | sustain | release |

### M.ENV 1 / M.ENV 2

Two mod envelopes, each an ADSR started by every note, with a destination and an amount of its own (ENV three and
four times).

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | attack | decay | sustain | release |
| DEST | LOOP ON / OFF | destination | amount (bipolar) | |

With **LOOP ON** an envelope runs attack -> decay -> attack -> decay ... for as long as you hold the key, then
releases: a sustain of 0 gives full up-and-down sweeps, a higher sustain a shallower cycle. Its destination can be
any of the LFO's (below), plus two of its own:

| | |
| --- | --- |
| LFORATE | the LFO's rate, up to +-4 octaves: an envelope that speeds the LFO up and slows it down |
| LFOAMT | the LFO's depth, from nothing up to double |
| LFOSHAPE | the LFO's wave, morphed: from the one you chose along SINE, TRI, SAW UP, SAW DN, SQUARE, S&H (a full amount: the whole row), each fading into the next |

The two can share a destination (they add up), or split the work: say M.ENV 1 sweeps the cutoff while M.ENV 2,
short and looping, flicks mod 1's pitch. The picture shows two seconds of the envelope (the key held for 1.5 s, the
dotted line where it is let go) with its destination's name, and with LFORATE or LFOAMT the LFO under it, moving as
the envelope moves it.

### LFO

One LFO per voice (or one shared, free-running). LFO 2 has a page of its own (below).

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| LFO | wave: SINE, TRI, SAW UP, SAW DN, SQUARE, S&H | rate (0.05 .. 40 Hz) | delay | retrig ON / OFF |
| DEST | destination 1 | amount 1 | destination 2 | amount 2 |

- **S&H** (sample and hold) jumps to a new random value each cycle.
- **Delay**: from each note, the LFO fades in over this time (0: at once).
- **Retrig ON**: each note starts its own LFO from the beginning; **OFF**: one LFO runs free and every note shares
  it.
- **Destinations** (two, each with a bipolar amount):

| | |
| --- | --- |
| PITCH | both oscillators, up to +-12 semitones (vibrato) |
| PITCH2 | osc 2 only (a wobbling detune against osc 1) |
| WAVE1, WAVE2 | the carrier's wave position, across the whole bank (each note moves on its own) |
| MWAVE1, MWAVE2 | both modulators' wave positions |
| MOD1, MOD2 | both modulators' amounts (FM brightness) |
| MPITCH | the modulators' pitch: the FM sidebands slide |
| CUTOFF | the filter (or the comb), up to +-48 semitones |
| AMP | tremolo |
| MIX | the balance between the two oscillators |
| SPREAD | the comb's spread, up to the whole knob either way |
| M1PITCH, M2PITCH | mod 1's / mod 2's pitch alone (both oscillators), up to +-12 semitones: in MIX a moving layer, in FM a sliding timbre |

The mod envelopes have pages of their own: **M.ENV 1 / M.ENV 2** (above).

### LFO 2

Press **LFO** again for LFO 2: one for all the voices, simpler than the LFO, but with many more destinations, and it
can run in time with the tempo.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| LFO | wave (as the LFO's) | rate (0.05 .. 40 Hz) | sync: OFF, 4/1, 2/1, 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 | |
| DEST | destination | amount | | |

**LFO 2** runs free, one for all the voices (so every note moves together). With **SYNC** on a division it ignores
RATE and runs in time with the arp and sequencer's tempo (the BPM on the ARP page, or the MIDI clock when it follows
one): 1/4 is a cycle a beat, 1/1 a bar, 4/1 four bars; T, triplets. It starts from the top when the sequencer or the
MIDI clock starts. Its destination can be any of the LFO's, or one of these (each moves the knob up and down from where
it is set, as far as the whole knob):

| | |
| --- | --- |
| RES | the filter's resonance (or the comb's feedback) |
| DRIVE | the filter's drive |
| HPF | the high-pass cutoff |
| DAMP | the comb's damping |
| FENV | the filter envelope's amount, up to +-60 semitones |
| LEVEL1, LEVEL2 | the oscillators' levels (the MIX page's) |
| DETUNE | osc 2, up to +-1 semitone |
| MENVAMT | the mod envelope's amount |
| LFOSHAPE | the LFO's wave, morphed along its shapes (as the mod envelopes' LFOSHAPE) |
| CHORUS, DELAY, REVERB | the effects' mixes (from 0: an effect that comes and goes) |
| DLYTIME | the delay time (the repeats bend, as tape does) |
| DLYFDBK | the delay's feedback |
| REVSIZE | the reverb's size |

### MATRIX

Press **LFO** a third time for the mod matrix: four more routes, each a **source** onto any of the LFO's destinations
(the same list as the LFO's DEST), with an **amount** either way. They add to what the LFO, LFO 2 and the mod
envelopes already do. A route is off while its source is OFF or its amount is 0 (the middle of the knob).

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| MOD 1 .. MOD 4 | SRC: the source | DEST: the destination | AMT: -64 .. +63 | |

| Source | |
| --- | --- |
| LFO, LFO 2 | the LFOs, as they run (a second route from the same LFO, at its own amount) |
| M.ENV 1, M.ENV 2 | the mod envelopes |
| VELO | how hard the note was played (the FM-1's keys: full; MIDI: as played) |
| KEY | the note: 0 at middle C, up above it, down below (five octaves either way: the whole amount) |
| WHEEL | the MIDI mod wheel (CC 1) |
| RANDOM | a new value at each note, held while it plays |

The picture lists the four routes, source to destination, each amount a bar from the middle. Some ideas: VELO onto
the CUTOFF (a harder note, brighter); KEY onto MOD1 (high notes less FM, so they stay clean); WHEEL onto the LFO's
pitch depth for vibrato on demand; RANDOM onto the pitch, a little, for an old synth's drift.

### MIX

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| OSC | osc 1 level | osc 2 level | osc 2 semitones (+-24) | osc 2 fine (+-50 cents) |
| ROUTE | osc 1: FILTER / DIRECT | osc 2: FILTER / DIRECT | the noise: FILTER / DIRECT | the noise's level |
| GLIDE | glide time (0 = off) | mode: ALWAYS / LEGATO | | |
| SPLIT | the split key (OFF, or a note) | the low side's octave (-2 .. +2) | the high side's octave | LEARN |

**Routing.** Each source goes **through the filter** (its drive, the low-pass or the comb, the high-pass), or
**DIRECT**: past it, straight to the amp (its envelope, VCA OPEN, the effects all still apply). Filter one
oscillator and leave the other bright: a resonant sweep over a steady body, or a comb ringing on the noise while
the oscillators stay clean. The picture shows each source's level and its route (DIR inverted).

**Glide** (portamento) slides each new note from the pitch of the last one. **ALWAYS** slides every note;
**LEGATO** only when another note is still held (play overlapping to slide, apart to jump). Arpeggiated notes glide
too: with a tied gate and LEGATO, an acid line.

**Split** the keyboard: below the split key only **OSC 1** plays, from it upward only **OSC 2**, so the two
oscillators become two sounds side by side (a bass under a lead). Set the key with KNOB1, or turn **LEARN** and
press the key you want (it is not played; a MIDI keyboard's note works too). OCT1 and OCT2 move each side up or down
by octaves (a bass played in the middle of the keys, sounding low). The four voices are shared: any mix of the two
sides, four notes at a time. The filter, the envelopes, the LFOs and the effects are the same for both; the noise
plays on both sides. The sequencer, the arpeggiator and MIDI follow the split too, by each note's pitch. It is saved
with the patch, the key and both octaves, so each patch has its own split (or none) and loading a patch brings its
split with it; a change waits for SAVE like any other. The picture shows it under the levels (OSC1 < C4 < OSC2).

### ARP

The arpeggiator plays the notes you hold, one at a time, in time. **ARP** (pressed again on the ARP page) or the ARP
cell turns it on or off (its LED lights). While it is on, the keys and MIDI notes go to it instead of straight to
the voices.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ARP | ON / OFF | mode: UP, DOWN, UPDN, RAND, ORDER | step: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | octaves: 1 .. 4 |
| TIME | tempo: 40 .. 240 BPM | gate: 5 .. 100 % (TIE) | swing: 50 .. 75 % | LATCH ON / OFF |

- **UP / DOWN** by pitch, **UPDN** up and back (the top and bottom once), **RAND** any of them, **ORDER** as you
  pressed them. **Octaves** repeat the pattern higher.
- **Gate** is how long each note sounds in its step; at **TIE** they join with no gap (with glide LEGATO: slides).
- **Swing** lengthens every other step and shortens the next: 50 % is straight, 67 % a triplet feel, 75 % a dotted
  one.
- **LATCH**: the notes keep playing after you let go; the next chord (after all the keys are up) replaces them.
- Its notes start and stop to within a third of a millisecond, whatever the screen is doing.
- The picture shows the next 16 steps, as long and as far apart as they will be, the one playing in white.

**On the MIDI clock** (SETUP: CLOCK = MIDI) the arpeggiator follows a DAW or another instrument instead of its own
tempo: 24 clock ticks a beat, a step every 24, 12, 8, 6, 4 or 3 ticks for 1/4 .. 1/32. **START** plays the pattern
from its first note, **STOP** stops it, **CONTINUE** goes on. Each step lands on its tick to within a third of a
millisecond, one audio buffer (5.8 ms) later. The BPM cell then shows the clock's tempo (**EXT** while none comes);
swing and gate still apply. With CLOCK = MIDI and no clock running, the arpeggiator waits.

### FX

Effects after the voices: **crush -> mod FX -> chorus -> delay -> reverb**, a page each: **FX** steps through
CHORUS, DELAY, REVERB, MOD FX and CRUSH. Each one is off while its **MIX** (CRUSH: its BITS and RATE) is at 0. The
chorus, delay and reverb pages draw the chain, with a mix bar under each effect and the page's own lit. CRUSH and
MOD FX work on the synth's voices only, before the chorus: the drums have a crush of their own (DRUM page, BUS row).

| Page | Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- | --- |
| CHORUS | CHORUS | rate | depth | mix | |
| DELAY | DELAY | time (10 .. 735 ms) | feedback | mix | PING ON / OFF |
| DELAY | D.MOD | wow rate | wow depth | shift (-12 .. +12 st) | tone |
| REVERB | REVERB | size | damping | mix | pre-delay (up to 93 ms) |
| REVERB | R.MOD | rate | depth | shimmer | |
| MOD FX | MOD FX | TYPE: OFF, PHASER, FLANGER | rate | depth | mix |
| MOD FX | TONE | MANUAL: where it sweeps around | FDBK: feedback, either way | | |
| CRUSH | CRUSH | BITS: OFF, 15 .. 2 bits | RATE: OFF, 44100 .. 1378 Hz | | |

- **Chorus**: left and right swept against each other, for width. Full mix is half wet: the classic chorus.
- **Delay**: **ping** sends the repeats from side to side. **Tone** darkens each repeat. Turning the time while it
  plays bends the repeats, like tape.
- **Wow**: sweeps the delay time, so the repeats wobble in pitch.
- **Shift**: each repeat is pitch-shifted: +12 makes the echoes climb an octave at a time, -5 makes them fall in
  fourths. (The first repeat is the note as played.)
- **Reverb**: a plate. **Size** sets how long the tail lasts, **damping** how quickly its highs fade, **pre-delay**
  the gap before it starts.
- **R.MOD**: sweeps the reverb's inside, so the tail shimmers and wobbles around the note.
- **Shimmer**: the tail climbs in octaves as it fades. It never runs away, at any setting.
- **Phaser**: six stages, their notches swept up and down by the rate and depth, around MANUAL (low: a deep, slow
  whoosh; high: a thin one). Feedback deepens the notches; turned down (negative), it hollows the sound instead.
- **Flanger**: a short delay (0.2 .. 8 ms, MANUAL) swept against the dry sound: a jet. With feedback it rings at the
  delay's pitch; negative feedback gives the hollower, metallic flange. MIX at about half is the classic one (full mix
  is all delay, a vibrato).
- **Crush**: BITS takes the sound down to fewer bits (grit at 12, broken at 4), RATE holds each sample longer (a
  lower sample rate, with the aliasing of an old sampler). The picture is a sine as it comes out.

With every effect on, Prysm uses roughly twice the CPU: watch the header.

### SEQ

A polyphonic step sequencer, 303-style: notes and **chords** of up to four notes, rests, **accents**, **slides** and
**ties**, in **4 patterns** of up to 64 steps, and a **song** that plays the patterns in an order you choose, each so
many times. Enter steps one at a time, or **record in real time** as it plays. It plays alongside the keys (and
the arpeggiator), at the ARP page's tempo and swing, on its own clock or the MIDI clock. Each patch slot has a
sequencer of its own (the patterns, the song, the drum kit): LOAD brings it in with the sound. To try other sounds
over the same song, load them with SOUND instead (see [Patches](#patches)).

The picture is the pattern being edited: 64 squares, 16 a row. An empty square is a rest, a grey one a note, a white
one an accent; a little bar joins a sliding step to the next; a tie is a bar carrying the note before it on; past the
length, dots. A chord's other notes are notches across the top of its square (a short notch: one of them tied on). The cursor is ringed (doubled while recording); a mark under a square shows the step playing. The
header says which pattern you are editing (**SEQ P1** .. **P4**), or **SEQ SONG** in song mode. On the SONG and
ENTRY rows the picture is the song instead: its entries, each a pattern and how many times it plays.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| STEP | the step (the cursor) | its note | its trigger: REST, NOTE, ACCENT, TIE | SLIDE ON / OFF |
| CHORD | the step's 2nd note (-- none) | 3rd note | 4th note | its velocity: AUTO (100, an accent 127) or 1 .. 127 |
| LOCK | LANE1: the parameter of the pattern's first lock lane (OFF: none) | VAL1: the step's value (-- none) | LANE2 | VAL2 |
| PATTERN | the pattern edited: P1 .. P4 | its length: 1 .. 64 | step: 1/4 .. 1/32 (all patterns) | gate (all patterns) |
| EDIT | the row: 1 .. 4, or ALL | COPY | PASTE | CLEAR |
| PARTS | MUTE1: part 1 PLAY / MUTE | MUTE2: part 2 | CLR1: clear part 1 | CLR2: clear part 2 |
| CLICK | the metronome: OFF, ON, REC (only while recording) | its level | | |
| SONG | MODE: PATT or SONG | the song's length: 1 .. 16 entries | tempo (the arp's) | INIT |
| ENTRY | the entry: 1 .. 16 | its pattern: P1 .. P4 | REPEAT: x1 .. x16 | |

**Entering notes** (stopped): press **REC** (its LED lights, the header says SEQ REC), move to a step with KNOB1 (or
start at the first) and **play a key**: its note goes into the step (a rest or a tie becomes a note), and when you let
go the cursor moves to the next, so you can type a line in. **Hold keys together** for a chord: they all go into the
step, and the cursor moves on once you let them all go. MIDI notes enter the same way. Press REC again to play the
keys normally.

**Recording in real time** (playing): press **PLAY**, then **REC**, and play. Or press **REC** first, then **PLAY**:
you get a **count-in**, a bar of ticks (whatever CLICK is set to), and recording starts on the downbeat; a note
played just before it lands on step 1. (On the MIDI clock there is no count-in: the clock's START is the
downbeat.) Each note lands on the nearest step
(the one playing, or the next if you are past its middle), with its velocity (the FM-1's keys: full; a MIDI
keyboard: as played), and a held note is tied over the steps you held it through. Recording **overdubs**: what is
there stays, and each pass adds to it, up to four notes a step (playing a note that is already in the step records
it again, at the new velocity). While you hold a key, the pattern's note in that place rests, so you hear yourself,
not both. You can record from any page (turn the filter as you go; on the DRUM page the drum keys record hits, see
[DRUM](#drum)); REC or PLAY ends it. To take something out, use
the STEP and CHORD rows, or CLEAR a row.

- **PLAY** starts from the first step (of the pattern, or of the song) and stops.
- **The metronome** (CLICK row) ticks every beat while the sequencer plays, higher on each bar's first beat (4/4,
  counted from PLAY), whatever the step size. **REC** ticks only while you record. It is dry (no effects on it), goes
  out over USB audio with the rest, and stays as you set it when you load another sound (it is a setting, as the
  MIDI ones are).
- **ACCENT** plays the step at full velocity, the others a little softer: how much louder, brighter and more
  modulated an accent is comes from the AMP page's **VEL** row.
- **SLIDE** holds the step's note into the next one and glides to its pitch in about 30 ms, **without a new attack**
  (the 303 slide). A slide into a rest just ends. In a chord, each note slides to the next step's note in its place.
- **TIE** carries the note before it on, at its pitch, without a new attack: a note followed by two ties is one note
  three steps long (its gate ends in the last tie). A tie after a rest is a rest. A chord's notes tie each on their
  own: a recorded chord whose notes were let go one by one keeps them as you played them.
- **Chords** play as up to four voices: the same four the keys use, so a four-note chord over a held chord steals
  the oldest notes.
- **Patterns**: PATT on the PATTERN row picks the one you edit; in PATT mode it is also the one playing. Changing it
  while the sequencer plays switches at the end of the current pattern, so it stays in time. Each pattern has its own
  length; the step and the gate are the same for all.
- **Copy and paste** (EDIT row): pick a row of 16 steps with KNOB1, turn **COPY**, pick another row (or another
  pattern on the PATTERN row) and turn **PASTE**. With the row on **ALL**, COPY and PASTE take the whole pattern,
  its length included; a single row pasted into ALL fills all four rows. **CLEAR** empties the row (with ALL, the
  whole pattern: turn it again within 3 seconds to confirm).
- **Two parts** (PARTS row), with the [split](#mix) on: the notes below the split key are **part 1** (they play
  OSC 1), from it upward **part 2** (OSC 2). Record a bass line on the low keys, then on the next passes a lead on
  the high keys: each pass overdubs, and while you hold a key only its own part's notes rest, so the other part keeps
  playing under you. **MUTE1** and **MUTE2** silence a part (saved with the sequencer); **CLR1** and **CLR2** clear
  a part's notes in the EDIT row's row (or ALL), leaving the other part and the drums. A step holds up to four notes
  across both parts. Which part a note is in follows the split: move the split key and notes change sides. With the
  split off, every note is part 1's.
- **The song** (SONG and ENTRY rows): set MODE to SONG, its LENGTH to the number of entries, then for each entry its
  pattern and how many times it plays. PLAY then plays the entries in turn and starts again after the last.
- **INIT** (SONG row) starts over: all four patterns empty, the song P1, P2, P3, P4 once each, PATT mode. It asks
  first: turn it again within 3 seconds.
- **Saving**: the patterns and the song save themselves into the slot whose sequencer it is, a few seconds after a
  change, once nothing is playing (the flash write would cut a note short). They come back at power-on. From Prysm
  1.2 and older (one sequencer for all), every used slot starts with a copy of it; from 0.8 and older, the pattern
  of the sound that comes back at power-on becomes pattern 1.
- **On the MIDI clock** (SETUP: CLOCK = MIDI): START plays from the first step, STOP stops, as for the arpeggiator;
  PLAY must be on.
- **Parameter locks** (LOCK row): each pattern has two lock lanes, each one a parameter: CUTOFF, RES, F.ENV (the
  filter envelope's amount), DRIVE, A.DEC and A.REL (the amp envelope's decay and release), F.DEC (the filter
  envelope's decay), FM1 and FM2 (the oscillators' modulation), DLY SND or REV SND. Pick the step with the STEP row's
  cursor (KNOB1), then on the LOCK row set its value with VAL1 or VAL2: the notes that start on that step play
  with the knob there instead of where the sound has it, and the next step without a lock goes back. A locked step has
  a mark in its corner. A lock is part of the step (the EDIT row's COPY, PASTE and CLEAR take it with the notes; a
  lock pasted into a pattern whose lane is OFF brings the lane's parameter), the lane's parameter part of the
  pattern. **DLY SND** and **REV SND** set how much of the synth goes into the delay and reverb
  for that step alone: a single echo thrown on the last note of a phrase, a snare-like reverb burst on one chord.

### DRUM

A drum machine of four drums, **KICK**, **SNARE**, **HAT** (closed; an accent opens it) and **CLAP**, each a lane of
steps in every pattern, under the notes: they play together, from the same steps, in the song too. **SEQ** twice. The
sounds were made from scratch (no recordings) in the style of the classic drum machines, in three kits (KIT row):

| Kit | |
| --- | --- |
| **LO-FI** | stored as an old sampler would keep them, 8 bits at a low rate (11 kHz the kick, 16 kHz the snare and clap, 22 kHz the hats), played back without smoothing: crunchy by design (Prysm 1.3's kit, with a clap) |
| **TR** | clean and bright, 16 bits: a punchy kick, a crisp snare, metallic hats |
| **BOOM** | 16 bits, deep: a long, low kick, a fat snare, a wide clap |

The kit (which one, the levels, tuning, decay, the bus) is one for every pattern, and, like the patterns, part of the
slot's sequencer: LOAD brings it in, SOUND leaves the beat going. Kits of your own can be built into the firmware
from WAV files (`tools/gen_drums.py --kit NAME=DIR`, then build): only use sounds you have the rights to.

The picture is the 16 steps the cursor is in, a lane a drum: a hit grey, an accent white, a flam white and grey, a
hat step with a roll cut into its hits, past the pattern's length dots; the drum you are editing named bright; a mark
under the step playing. Below, that drum's sound as it will play (its length tuned, DECAY over it), on the ROLL row the
step's hits, each as loud as it plays, or on the BUS row its settings.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| STEP | the drum: KICK / SNARE / HAT / CLAP | the step (the cursor) | its HIT: --, HIT, ACCENT (the snare: FLAM too) | CLEAR the drum's lane (turn twice) |
| ROLL | the step (the cursor) | the hat step's ROLL: OFF, 2, 3, 4, 6, 8 hits | RAMP: EVEN; down: FADE; up: SWELL | |
| KICK, SNARE, HAT, CLAP | LEVEL (0: silent) | TUNE: +-24 semitones | DECAY: FULL (as made), down: shorter | FILTER: how much goes through the sound's filter |
| BUS | DRIVE | CRUSH: fewer bits and a lower rate still | DELAY send | REVERB send |
| KIT | LO-FI, TR, BOOM (and any of your own) | | | |
| EDIT | the pattern: P1 .. P4 | LANE: ALL, KICK, SNARE, HAT, CLAP | COPY (turn) | PASTE (turn) |

**Copying drum patterns** (EDIT row): pick the pattern with KNOB1 and the LANE with KNOB2, turn **COPY**, pick
another pattern and turn **PASTE**. Only the drums change; the pattern's notes and length stay as they are. The copy
fills the pattern's length, repeated if it was shorter (a 16-step beat pasted into a 32-step pattern plays twice), and
steps past the length are left alone. With LANE on ALL, all four drums go to their own lanes. Copy one lane and paste
it into another to move a part, such as the kick's beat onto the snare (a flam becomes an accent off the snare, and
rolls stay on the hat). Copy one lane and paste with LANE on ALL to put it back in its own lane. The drum clipboard is
separate from the SEQ page's, which copies notes and drums together.

**On the DRUM page the keys are the drum machine's**, as on a TR (they play no notes here):

- The **16 white keys** (F3 .. G5) are the 16 steps on screen. Press one: a hit; again: an accent; (the snare) again: a
  flam; again: none. Their **LEDs** show the lane (an accent blinks, a flam faster; the step playing flips its LED as
  it passes).
- **F#3, G#3, A#3, C#4** (the first four black keys) pick KICK, SNARE, HAT and CLAP, and play it.
- **Recording drums in real time**: on the DRUM page, press **REC** (the header says DRUM P1 REC; the page stays) and
  **PLAY** (with the count-in if REC came first), then tap **F#3, G#3, A#3, C#4** in time. Each hit lands on the nearest
  step, as notes do (the one playing, or the next if you are past its middle), and you hear it once, as you hit it.
  Recording overdubs: a hit, accent, flam or roll already there stays. Add accents and flams with the white keys.
- **F#4 / G#4**: the 16 steps before / after, for patterns longer than 16 (KNOB2, the step, does it too).
- On the **ROLL** row the white keys set the hat's rolls instead: each press the next, OFF, 2, 3, 4, 6, 8, then OFF
  (on an empty step it puts a hit there too). A step with a roll flickers its LED.

- **Accents** play at full level, hits a little softer; the hat's accent is the **open hat**, and the next hat (open
  or closed) cuts it off, as on an 808.
- **A flam** (the snare's) is two snares: a hit, then ~20 ms later a softer one, the first ringing on under it.
- **A roll** (the hat's, also called a ratchet) plays the step 2 .. 8 times, evenly over its length: the trap hat. 3
  and 6 are triplets; on a 1/16 step, 2 is a 1/32 and 4 a 1/64. Each hit cuts the one before off (an open hat's too);
  a swung step spreads them over its swung length. **RAMP** shapes the hits: EVEN, all at the step's level; FADE, each
  softer than the last (at most to a quarter); SWELL, rising into it. RAMP is one for every roll in the sequencer,
  saved with the kit. Taking a step's hit away takes its roll.
- **The sends** feed the drums into the sound's **delay** and **reverb**: what comes back is that effect's MIX on its
  page (a mixer's send and return), so with the effect's MIX at 0 nothing comes back. The rest of the drums stay dry.
- **FILTER** sends a drum through a filter of the drums' own that follows the sound's **CUTOFF** and **RES** (and
  its type: LP 24, or LP 12 for the others; no envelope, and it does not self-oscillate). 0: the drum dry, 127: all
  of it filtered, between: a blend. Turn the cutoff and the hat and snare sweep with the synth while the kick stays
  full, or the other way round.
- **CRUSH** holds each sample for up to 16 frames and cuts it to as few as 4 bits: from a little grit to a broken
  toy. **DRIVE** saturates the drums' bus before it.
- **Copy, paste, clear** on the SEQ page's EDIT row take the drums (and the rolls) with the notes. **INIT** clears them too and puts
  the kit back as new.
- **MIDI**: notes on **channel 10** play the drums (General MIDI's: 36 kick, 38 or 40 snare, 42 or 44 closed hat, 46
  open hat, 39 clap), whatever the IN channel, with their velocity. So a DAW's drum track plays them.

### LOOP

**SEQ** three times: a MIDI looper. It records the **notes** you play (the keys and MIDI in: which, when, how hard,
not the audio), then plays them back through the synth, round and round, in time with the sequencer and arpeggiator's
tempo, while you play on top. Because it records notes, the loop plays whatever the sound is now: change the patch or
turn the filter and the loop follows.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| LOOP | BARS: 1, 2, 4, 8 (the next loop's length) | QUANT: OFF, 1/16, 1/8 | PLAY: ON / OFF | CLEAR (turn) |

- **Recording**: on the LOOP page press **REC**. With the sequencer playing, recording starts at the next bar line;
  stopped, it starts with the first note you play. After BARS bars the loop closes and plays back, still recording:
  an overdub, until you press **REC** (the header's REC goes out, PLAYING).
- **Overdub**: press **REC** again while it plays and what you play is added on the next passes. **REC** once more
  ends the overdub. (Pressed during the first pass, it stops recording there; the loop stays BARS long.)
- **QUANT** moves each note's start to the nearest 1/16 or 1/8 as you record it (OFF: as played).
- **PLAY** stops and starts the loop; **CLEAR** empties it.
- The picture is the loop: its bars and beats, each note a dash at its pitch from its start to its end, the playhead,
  and what the looper is doing. The header says **REC** while it records.
- It holds up to 512 notes. Its notes play on the synth's four voices, with the keys and the sequencer. The loop is
  not saved: power off and it is gone.

### SETUP

Settings for your setup rather than a sound: they are kept with the octave, not in patches. **GLO** twice.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| MIDI | IN: OMNI or 1 .. 16 | OUT: 1 .. 16 | CLOCK: INT / MIDI | BEND: OFF, +-1 .. +-24 semitones |

- **IN**: the channel Prysm plays from (OMNI: all of them).
- **OUT**: the channel the FM-1's keys send their notes on.
- **CLOCK**: the arpeggiator's tempo: its own (**INT**, the BPM on the ARP page) or the **MIDI** clock.
- **BEND**: how far a MIDI keyboard's pitch wheel bends every note, either way (2 at first; OFF ignores it).

---

## Patches

Press **SAVE** for the PATCH screen. The eight slots are drawn as tiles: the slot's number and a picture of its
osc 1 wave (or EMPTY). The selected slot has a double edge; a dot marks the slot the current sound came from. Under
them: what SAVE will do, then what happened (framed when it asks you to press SAVE again); along the bottom, the
tabs SAVE, LOAD, SOUND, INIT, UNDO.

| Control | Does |
| --- | --- |
| **SELECT** | SAVE, LOAD, SOUND, INIT or UNDO |
| **KNOB1** | the slot |
| **SAVE** | does it |
| **HOME**, ALGORITHM or a page button | back |

- **SAVE** writes the current sound and the sequencer playing (with any changes not yet saved) to the slot, and only
  to it: saved to another slot, the slot the sequencer came from keeps what it last saved. Over a slot holding another sound it asks
  first: press SAVE again. It asks too when the slot's sound came in by SOUND (REPLACES ITS SEQ): its own sequencer
  would make way for the one playing.
- **LOAD** brings the slot's sound back, and its sequencer. While the sequencer plays, the new one takes over where
  the next pattern would start (the sound changes at once), from the top of its pattern or song; stopped, at once.
  A slot whose sound came in from the editor alone has no sequencer yet: LOAD keeps the one playing, and it becomes
  that slot's. LOAD does not save the sequencer it replaces: pattern changes it has not saved yet (they save a few
  seconds after a change, once nothing plays) are let go.
- **SOUND** brings the slot's sound back alone: the sequencer plays on, still saving into the slot it came from.
- **INIT** loads the default sound (the sequencer plays on). If you have unsaved changes, LOAD, SOUND and INIT ask
  first: press SAVE again.
- **UNDO** takes the last change back: the sound and the sequencer as they were before it. A change is a knob turned
  after a pause (2 seconds: a sweep of turns is one change), or an action: a PASTE, a CLEAR, a LOAD, SOUND or INIT.
  The screen says what it will undo; press **SAVE**. Press SAVE again to redo. It is one step: the change before that
  is gone. It does not undo a save (the slot keeps what was saved), and the looper is apart from it.
- The header shows the current slot on every page, with **\*** when there are unsaved changes.
- When the FM-1 is switched on, the slot last saved or loaded comes back, and the sequencer that was playing.
- Saving takes a moment (the flash is erased): a note playing drops out for about 50 ms.
- Patches are kept in two flash sectors written in turn: if the power goes during a save, only that save is lost.
- Patches saved by an older Prysm load in a newer one (the new parts start as the default sound has them). From
  Prysm 0.8 and older: a modulator in MIX or RING gets the pitch of its ratio (x2: +12, x1.5: +7); ratios of 5 and
  up are further than PIT goes, and come in at +24.

---

## The oscilloscope

Press **HOME** for a live scope of what Prysm plays (before the MASTER knob, so it does not shrink as you turn it
down): **left in white, right in grey** (the chorus and a ping-pong delay show as the two apart).

- It locks onto the **newest note**: the trace shows that many of its cycles, starting on a rising zero, so the
  wave stands still whatever you play. **KNOB1** sets how many cycles (1 .. 8; it starts at 2).
- It **zooms by itself** (x1 .. x8, shown under the trace), so a single quiet note fills the screen too.
- **HOME** again opens the [LIVE screen](#the-live-screen). ALGORITHM, SAVE or a page button go back.

The scope costs no sound processing while it is not shown.

---

## The LIVE screen

For playing live: change patterns and patches in time, with four knobs of your choosing. Press **HOME** twice (the
scope, then LIVE).

```
LIVE > PLAY                      2
PATTERN                       PATT
[ P1 ][#P2#][ P3 ][ P4 ]          the pattern playing filled, the one waiting framed, blinking
PATCH  BD SD HH CP O1 O2 NZ        the mutes: filled while on, dim while muted
[1][#2#][3][4][5][6][7][8]        the same for the patches (empty slots dim)
====  ----  ----  ----            the bar: its four beats, the one playing lit
   SELECT: CHOOSE A KNOB          a hint (on K1 .. K4: PRESETS: ITS PARAMETER)
FILTER FILTER DELAY REVERB         each knob's page
 CUT    RES    MIX    MIX          KNOB1 .. KNOB4
```

| Control | Does |
| --- | --- |
| **The first 4 black keys** (F#3 G#3 A#3 C#4) | a pattern, P1 .. P4 |
| **The first 8 white keys** (F3 .. F4) | a patch, slot 1 .. 8: its sound and its sequencer |
| **G4, A4, B4, C5** | mute the kick, the snare, the hats, the clap (press again: on) |
| **D5, E5** | mute OSC 1, OSC 2 |
| **F5** | mute the noise |
| **G5** | unmute all (lit while anything is muted) |
| **KNOB1 .. KNOB4** | the parameters chosen for them |
| **SELECT** | PLAY, or K1 .. K4: the knob PRESETS chooses for |
| **PRESETS** | on K1 .. K4: that knob's parameter (any of the sound's, page by page); on PLAY: the cutoff, as everywhere |
| **HOME** or ALGORITHM | back to where you first pressed HOME |
| **SAVE**, a page button | the PATCH screen, that page |

- While the sequencer plays, a change **waits for the next bar line** (4 beats) and blinks until then; it starts
  from the top of its pattern. Stopped, it happens at once. Press a waiting key again to cancel it.
- A **pattern** key in SONG mode drops the sequencer to PATTERN mode, playing that pattern.
- A **patch** key brings the slot's sound and its sequencer in together on the bar line (the new sound follows
  within a few milliseconds). Unsaved changes to the sound playing are let go, as on a synth's program change; the
  pattern chosen is kept with the sequencer.
- **The mutes** act at once and on everything: the sequencer, the keys, MIDI. A muted drum's new hits are silent; a
  muted oscillator or the noise goes quiet in every voice. Their keys are lit while on and flash while muted. They
  hold only on the LIVE screen: leave it and all are on again (they are not saved either).
  (They are apart from the SEQ page's MUTE1 and MUTE2, which mute a part of the sequencer and are saved with it.)
- The other keys play as usual: D#4 and the white keys from G4 to F5.
- Which parameter each knob turns is a setting, kept for every patch and after power-off. At first: the cutoff,
  the resonance, the delay's mix and the reverb's mix. Turning a knob changes the sound (the header's **\*** shows it).

---

## USB: audio and MIDI

Connected to a computer, the FM-1 shows up as **Prysm FM-1**:

- a **2-in audio input** (16-bit stereo, 44.1 or 48 kHz): record Prysm straight into any DAW, no cable needed. It
  carries exactly what the speaker plays, effects included.
- a **MIDI port**: notes in play Prysm (with their velocity, on the SETUP page's IN channel); channel 10 plays the
  drums; the keys' notes come out on its OUT channel; the MIDI clock can drive the arpeggiator.

Prysm's USB audio is checked on macOS 14, where the audio of many FM-1 firmwares does not appear.

### Filming a performance

`tools/capture.py` makes a video of what you play: the screen exactly as the FM-1 draws it, with its sound, in sync.
Connect the FM-1 by USB, then, in the `Prysm` folder:

```
.venv/bin/python3 tools/capture.py record mytake
```

Set the FM-1 up (the page and sound you want to start on, no keys held), press **Enter**, play, press **Enter**
again. The FM-1 sends what it started from and everything you did (keys, buttons, knob turns, MIDI coming in) to the
computer, which plays the take again through Prysm's own code, frame by frame, and writes `takes/mytake.mp4`
(720 x 720, 30 fps) and `takes/mytake/audio.wav`. Options: `--fps 60`, `--size 1080`, `--tail 5` (seconds after the
last thing you did, for the release and the reverb), `--splash` (start with the power-on animation);
`tools/capture.py render takes/mytake --fps 60` films a take again. It needs ffmpeg
(`.venv/bin/pip install imageio-ffmpeg`, or `brew install ffmpeg`).

Not in the film: the **MASTER** knob (it is at full level), and the MIDI clock (a take played on CLOCK = MIDI
uses the internal tempo).

---

## The editor

[The editor](https://nicholaspratt.github.io/Prysm/editor/) (Chrome or Edge) is Prysm itself, running in the
page: the screen, the knobs (drag or scroll), the buttons and the keys (or your computer's keys, A W S E D ... from C4),
with the sound. Beside it, a tab for every page with every parameter, the sequencer's 64 steps as a grid, and patch
files to save and load. The browser keeps its own 8 slots between visits.

**With an FM-1** (Prysm 1.0 or later): connect it by USB and press **Connect FM-1** (allow MIDI when the browser
asks). The editor takes the FM-1's current sound, and from then on the two stay in step: what you change in the editor
the FM-1 plays (its knob's value shows in the corner, as if you had turned it), and what you turn on the FM-1 shows up
in the editor. **Store on FM-1** saves the sound in one of its slots. Another MIDI keyboard connected to the computer
plays the browser's Prysm.

---

## If something goes wrong

- **It will not start / a black screen after an update**: hold **OCT-** while switching the FM-1 on. That starts
  the rescue updater (no sound, just the update port): install Prysm, or another firmware from the Firmware Hub.
- **A crash**: the screen shows where, and the FM-1 restarts. Two crashes in a row at start-up go to the rescue
  updater by themselves.
- **The installer cannot find the FM-1**: use Chrome or Edge, a USB cable that carries data, plugged straight into
  the computer; close other music apps that may hold the MIDI port.
- **The sound crackles**: the CPU is over its limit; lower the effects or the number of notes.

---

## Sound ideas

- **Electric piano**: OSC 1 carrier sine, mod 1 sine at x1 (or x14 for the tine), FM, a quick mod 1 attack and a
  medium release; AMP with a long decay and low sustain; chorus.
- **Evolving pad**: carrier on VOCAL, LFO TRI slow into WAVE1, mod envelope slow attack into LFOAMT; reverb with a
  big size and some shimmer.
- **Rhythmic bleeps**: LFO S&H into MPITCH with some FM; mod envelope LOOP ON into LFORATE; delay with ping and a
  little shift.
- **Bass**: carrier saw, osc 2 a square an octave down (MIX: -12 st), filter low with a fast envelope and some
  drive.
- **Bells**: carrier sine, mod 1 sine at x3 (or x1.5, x7), FM with a long release; delay with shift +12 and the tone
  down.

---

Prysm by Nicholas Pratt, built on [Dinghy](https://github.com/zvenson/dinghy) (zvenson), the platform of
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) and
[SLOOP](https://github.com/isod89/sloop-fm1) (isod89). GPL-3.0. M-VAVE and FM-1 are trademarks of their owners.
