# Prysm manual

Prysm is a 4-voice synthesizer firmware for the **M-VAVE FM-1**. Each voice has two oscillators, and each oscillator is a
little modulation chain of three wavetable operators. After them come a resonant filter (12 or 24 dB low-pass, or a comb), a high-pass, an amp envelope, an
LFO, a looping mod envelope and stereo chorus, delay and reverb.

- [Installing](#installing)
- [Playing](#playing)
- [Finding your way around](#finding-your-way-around)
- [The pages](#the-pages): [OSC 1 / OSC 2](#osc-1--osc-2), [FILTER](#filter), [AMP](#amp), [LFO](#lfo), [LFO 2](#lfo-2),
  [MIX](#mix), [ARP](#arp), [SEQ](#seq), [FX](#fx) (CHORUS, DELAY, REVERB), [SETUP](#setup)
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
python3 tools/fm1_install.py prysm-0.8.fwsc
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
| **USB-MIDI in** | notes play Prysm with their velocity (see AMP's VEL row), on any channel or the one set on SETUP |

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
| **EDIT** | OSC 1 (press again: OSC 2) |
| **ENV** | AMP (press again: FILTER) |
| **SELECT** (the button) | FILTER |
| **LFO** | LFO (press again: LFO 2) |
| **GLO** | MIX (press again: SETUP) |
| **ARP** | ARP (on the ARP page: the arpeggiator on / off; its LED shows it) |
| **SEQ** | SEQ |
| **PLAY** | the sequencer: run / stop (from any page; its LED shows it) |
| **REC** | step entry on the SEQ page (its LED shows it) |
| **FX** | CHORUS (press again: DELAY, then REVERB) |
| **HOME** | the oscilloscope |
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
| RATIO | carrier ratio | mod 1 ratio | mod 2 ratio | fine tune |
| MODE | mod 1's mode | mod 2's mode | | |
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

**Envelopes.** Each modulator has its own attack and release (1 ms .. 10 s): the amount rises over the attack,
holds while the key is down, and falls over the release. A slow mod 1 attack on FM makes a note that brightens as
you hold it.

**MIX row.** The modulators' amounts (how much they change the next one) and the oscillator's own level.

### FILTER

A filter for each voice, with drive before it, a high-pass after it, and its own envelope. TYPE picks what it is: a
**LP 12** (2-pole, 12 dB / octave) or **LP 24** (4-pole, 24 dB / octave) low-pass, or a comb, **COMB+** or **COMB-**.
The picture draws the filter's actual response (the low-pass's curve or the comb's peaks, and the high-pass) and the
envelope's shape.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| FILTER | cutoff (16 Hz .. 20 kHz) | resonance | envelope amount (bipolar, +-60 semitones) | keytrack (0 .. 100 %) |
| ENV | attack | decay | sustain | release |
| TYPE | LP 12, LP 24, COMB+, COMB- | drive (0 = clean) | high-pass cutoff (OFF, 16 Hz .. 4.2 kHz) | |
| COMB | damping (0 bright .. 127 dark) | spread | | |

Cutoff and resonance glide as you turn them, so sweeps are smooth. **Keytrack** at 100 % moves the cutoff with the
note, so every key keeps the same brightness. A negative envelope amount closes the filter as the envelope rises.

**LP 24** is steeper and darker than LP 12; its resonance is a little softer.

**COMB+ / COMB-** turn the filter into a comb, as on a Waldorf: a row of resonant peaks at a frequency and every
multiple of it. The FILTER row then plays the comb: CUT sets the first peak (in 3/4-semitone steps: 64 is C4, 80 is
C5), RES its feedback (from no effect to long ringing), ENV sweeps it with the envelope and KEY tracks the keyboard.
With KEY at 127 (100 %) and CUT at 64, the peaks sit on each note's own harmonics: a plucked string from a short
burst, with a long amp release. COMB+ rings on the peaks, metallic and plucked; COMB- puts them halfway between,
hollow and an octave down. On the **COMB** row, DAMP darkens the ringing and **SPREAD** stretches the upper peaks
further and further apart while the first stays put: from an even comb to bells, bars and metal. The peaks get
louder as RES rises (up to about +17 dB, rounded off rather than clipped), so the comb keeps its level even when they
miss the harmonics. High feedback takes a moment to build up on low notes.

The LFO and the mod envelope move the comb through **CUTOFF**, and its spread through **SPREAD**.

### AMP

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| ENV | attack | decay | sustain | release |
| VEL | velocity to level | velocity to cutoff | velocity to modulation | |

Times go from 1 ms to 10 s.

**Velocity** comes from MIDI: a keyboard or a DAW playing Prysm over USB. The FM-1's own keys have no velocity (they
are on / off), so they always play at full velocity and these settings do not change them. Each sensitivity says how
much a softer note is quieter (by the square of its velocity, as it sounds natural), darker (up to 36 semitones of
cutoff) or less modulated (less FM brightness: the classic expressive FM touch). At 0, velocity changes nothing.

### LFO

One LFO per voice (or one shared, free-running) and a mod envelope that can loop. LFO 2 has a page of its own (below).

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| LFO | wave: SINE, TRI, SAW UP, SAW DN, SQUARE, S&H | rate (0.05 .. 40 Hz) | delay | retrig ON / OFF |
| DEST | destination 1 | amount 1 | destination 2 | amount 2 |
| M.ENV | mod envelope attack | decay | sustain | release |
| M.DST | LOOP ON / OFF | destination | amount | |

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

**The mod envelope** (M.ENV, M.DST) is an ADSR started by each note. With **LOOP ON** it runs attack -> decay ->
attack -> decay ... for as long as you hold the key, then releases: a sustain of 0 gives full up-and-down sweeps, a
higher sustain a shallower cycle. Its destination can be any of the LFO's, plus two of its own:

| | |
| --- | --- |
| LFORATE | the LFO's rate, up to +-4 octaves: an envelope that speeds the LFO up and slows it down |
| LFOAMT | the LFO's depth, from nothing up to double |

On the M.ENV and M.DST rows the picture shows two seconds of the envelope (the key held for 1.5 s, the dotted line
where it is let go), and with LFORATE or LFOAMT the LFO under it, moving as the envelope moves it.

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
| CHORUS, DELAY, REVERB | the effects' mixes (from 0: an effect that comes and goes) |
| DLYTIME | the delay time (the repeats bend, as tape does) |
| DLYFDBK | the delay's feedback |
| REVSIZE | the reverb's size |

### MIX

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| OSC | osc 1 level | osc 2 level | osc 2 semitones (+-24) | osc 2 fine (+-50 cents) |
| GLIDE | glide time (0 = off) | mode: ALWAYS / LEGATO | | |

**Glide** (portamento) slides each new note from the pitch of the last one. **ALWAYS** slides every note;
**LEGATO** only when another note is still held (play overlapping to slide, apart to jump). Arpeggiated notes glide
too: with a tied gate and LEGATO, an acid line.

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

Stereo effects after the voices: **chorus -> delay -> reverb**, a page each: **FX** steps through CHORUS, DELAY
and REVERB. Each one is off while its **MIX** is 0. The picture draws the chain, with a mix bar under each effect
and the page's own effect lit.

| Page | Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- | --- |
| CHORUS | CHORUS | rate | depth | mix | |
| DELAY | DELAY | time (10 .. 735 ms) | feedback | mix | PING ON / OFF |
| DELAY | D.MOD | wow rate | wow depth | shift (-12 .. +12 st) | tone |
| REVERB | REVERB | size | damping | mix | pre-delay (up to 93 ms) |
| REVERB | R.MOD | rate | depth | shimmer | |

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

With every effect on, Prysm uses roughly twice the CPU: watch the header.

### SEQ

A step sequencer, 303-style: notes, rests, **accents**, **slides** and **ties**, in **4 patterns** of up to 64 steps,
and a **song** that plays the patterns in an order you choose, each so many times. It plays alongside the keys (and
the arpeggiator), at the ARP page's tempo and swing, on its own clock or the MIDI clock. The patterns and the song
are not part of a sound: they stay when you load another, so you can try sounds on the same song.

The picture is the pattern being edited: 64 squares, 16 a row. An empty square is a rest, a grey one a note, a white
one an accent; a little bar joins a sliding step to the next; a tie is a bar carrying the note before it on; past the
length, dots. The cursor is ringed (doubled while recording); a mark under a square shows the step playing. The
header says which pattern you are editing (**SEQ P1** .. **P4**), or **SEQ SONG** in song mode. On the SONG and
ENTRY rows the picture is the song instead: its entries, each a pattern and how many times it plays.

| Row | KNOB1 | KNOB2 | KNOB3 | KNOB4 |
| --- | --- | --- | --- | --- |
| STEP | the step (the cursor) | its note | its trigger: REST, NOTE, ACCENT, TIE | SLIDE ON / OFF |
| PATTERN | the pattern edited: P1 .. P4 | its length: 1 .. 64 | step: 1/4 .. 1/32 (all patterns) | gate (all patterns) |
| EDIT | the row: 1 .. 4, or ALL | COPY | PASTE | CLEAR |
| SONG | MODE: PATT or SONG | the song's length: 1 .. 16 entries | tempo (the arp's) | INIT |
| ENTRY | the entry: 1 .. 16 | its pattern: P1 .. P4 | REPEAT: x1 .. x16 | |

**Entering notes**: press **REC** (its LED lights, the header says SEQ REC), move to a step with KNOB1 (or start at
the first) and **play a key**: its note goes into the step (a rest or a tie becomes a note) and the cursor moves to the
next, so you can type a line in. MIDI notes enter the same way. Press REC again to play the keys normally.

- **PLAY** starts from the first step (of the pattern, or of the song) and stops.
- **ACCENT** plays the step at full velocity, the others a little softer: how much louder, brighter and more
  modulated an accent is comes from the AMP page's **VEL** row.
- **SLIDE** holds the step's note into the next one and glides to its pitch in about 30 ms, **without a new attack**
  (the 303 slide). A slide into a rest just ends.
- **TIE** carries the note before it on, at its pitch, without a new attack: a note followed by two ties is one note
  three steps long (its gate ends in the last tie). A tie after a rest is a rest.
- **Patterns**: PATT on the PATTERN row picks the one you edit; in PATT mode it is also the one playing. Changing it
  while the sequencer plays switches at the end of the current pattern, so it stays in time. Each pattern has its own
  length; the step and the gate are the same for all.
- **Copy and paste** (EDIT row): pick a row of 16 steps with KNOB1, turn **COPY**, pick another row (or another
  pattern on the PATTERN row) and turn **PASTE**. With the row on **ALL**, COPY and PASTE take the whole pattern,
  its length included; a single row pasted into ALL fills all four rows. **CLEAR** empties the row (with ALL, the
  whole pattern: turn it again within 3 seconds to confirm).
- **The song** (SONG and ENTRY rows): set MODE to SONG, its LENGTH to the number of entries, then for each entry its
  pattern and how many times it plays. PLAY then plays the entries in turn and starts again after the last.
- **INIT** (SONG row) starts over: all four patterns empty, the song P1, P2, P3, P4 once each, PATT mode. It asks
  first: turn it again within 3 seconds.
- **Saving**: the patterns and the song save themselves a few seconds after a change, once nothing is playing (the
  flash write would cut a note short). They come back at power-on. From an older Prysm, the pattern of the sound
  that comes back at power-on becomes pattern 1.
- **On the MIDI clock** (SETUP: CLOCK = MIDI): START plays from the first step, STOP stops, as for the arpeggiator;
  PLAY must be on.

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
tabs SAVE, LOAD, INIT.

| Control | Does |
| --- | --- |
| **SELECT** | SAVE, LOAD or INIT |
| **KNOB1** | the slot |
| **SAVE** | does it |
| **HOME**, ALGORITHM or a page button | back |

- **SAVE** writes the current sound to the slot. Over a slot holding another sound it asks first: press SAVE again.
- **LOAD** brings the slot's sound back. **INIT** loads the default sound. If you have unsaved changes, both ask
  first: press SAVE again.
- The header shows the current slot on every page, with **\*** when there are unsaved changes.
- When the FM-1 is switched on, the slot last saved or loaded comes back.
- Saving takes a moment (the flash is erased): a note playing drops out for about 50 ms.
- Patches are kept in two flash sectors written in turn: if the power goes during a save, only that save is lost.
- Patches saved by an older Prysm load in a newer one (the new parts start as the default sound has them).

---

## The oscilloscope

Press **HOME** for a live scope of what Prysm plays (before the MASTER knob, so it does not shrink as you turn it
down): **left in white, right in grey** (the chorus and a ping-pong delay show as the two apart).

- It locks onto the **newest note**: the trace shows that many of its cycles, starting on a rising zero, so the
  wave stands still whatever you play. **KNOB1** sets how many cycles (1 .. 8; it starts at 2).
- It **zooms by itself** (x1 .. x8, shown under the trace), so a single quiet note fills the screen too.
- **HOME** again, ALGORITHM, SAVE or a page button go back.

The scope costs no sound processing while it is not shown.

---

## USB: audio and MIDI

Connected to a computer, the FM-1 shows up as **Prysm FM-1**:

- a **2-in audio input** (16-bit stereo, 44.1 or 48 kHz): record Prysm straight into any DAW, no cable needed. It
  carries exactly what the speaker plays, effects included.
- a **MIDI port**: notes in play Prysm (with their velocity, on the SETUP page's IN channel); the keys' notes come
  out on its OUT channel; the MIDI clock can drive the arpeggiator.

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

**With an FM-1** (Prysm 0.8 or later): connect it by USB and press **Connect FM-1** (allow MIDI when the browser
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
