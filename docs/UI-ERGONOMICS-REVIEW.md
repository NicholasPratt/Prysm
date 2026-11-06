# Prysm UI ergonomics review

Reviewed 6 November 2026 using the existing UI screenshots, page definitions, and input handling in [`firmware/src/ui.c`](../firmware/src/ui.c). This is a review of the firmware interface, rather than hands-on testing of the hardware.

Prysm already has a coherent visual foundation. The biggest opportunity is to make navigation and knob assignments more predictable. Retain the waveform diagrams, four parameter cells, and restrained colours.

The following changes are listed in priority order.

## 1. Give each physical control a dependable role

SELECT currently opens FILTER in single-part mode, switches A/B in two-part mode, and needs a double press to reach FILTER there. That makes a familiar action depend on the loaded sound.

Keep a single press opening FILTER in every mode, with an explicit secondary gesture for switching parts. A hold could work if the hardware input handling supports it well. Always show `PART A`, `PART B`, or `GLOBAL` prominently so shared effects cannot be mistaken for part-specific controls.

PRESETS should also retain its cutoff function during performance. LIVE currently repurposes it when a knob assignment row is selected; put assignment editing behind an explicit setup mode.

## 2. Organise oscillator editing around the operator being edited

Currently WAVE combines carrier and MOD1, WAVE2 contains MOD2, and their ratios, modes, envelopes, and amounts are distributed across other rows. Editing one modulator requires remembering several locations.

A possible arrangement:

| Row | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| CARRIER | Bank | Wave | Ratio | Level |
| MOD1 | Bank | Wave | Ratio/Pitch | Amount |
| MOD1 ENV | Attack | Release | Mode | Fine |
| MOD2 | Bank | Wave | Ratio/Pitch | Amount |
| MOD2 ENV | Attack | Release | Mode | Fine |
| TUNE | Overall fine | — | — | — |

The diagram would highlight the corresponding operator. This keeps six rows while making each sound-building task more local. Prototype it against the current layout before committing, since the current ratio row is useful for comparing all three operators.

## 3. Standardise related knob positions

Some consistency already works well: ADSR is always left to right, and chorus/delay/reverb put MIX on knob 3. Extend those conventions.

MOD FX currently puts MIX on knob 4; move it to knob 3. Give modulation destination/amount pairs consistent positions wherever possible. Use the same terminology for levels, amounts, tuning, and routing across pages.

Muscle memory should transfer between related pages.

## 4. Make inactive controls visibly inactive

COMB controls remain available while the filter is LP12/LP24. LFO2 displays a free-running RATE alongside an active SYNC division. Those presentations invite adjustments whose effect is unclear.

Keep row positions stable, but dim inactive values and explain why: `COMB FILTER ONLY`, for example. For synced modulation, show the effective division prominently and clearly mark the free rate as inactive.

Also display envelope times in milliseconds/seconds using the engine’s actual mapping. Raw values such as attack `8` and release `60` are difficult to interpret.

## 5. Break up the sequencer’s ten-row menu by task

STEP, CHORD, PART, LOCK, PATTERN, EDIT, PARTS, CLICK, SONG, and ENTRY mix note editing, arrangement, and configuration. The scrolling tabs conceal much of that structure.

Group them into **Steps**, **Pattern**, and **Song**, with recording settings collected together. Show a row position such as `2/4` and retain the selected step while moving between groups.

The sequencer graphic could also communicate more: clear beat boundaries, step numbers, and distinct markers for the playback position and editing cursor. The drum grid already provides a stronger overview.

## 6. Use one clear interaction for actions

CLEAR, COPY, PASTE, and LOAD sometimes appear as knobs labelled `TURN`; patch operations use SAVE to execute, including loading and undoing.

Establish a consistent pattern: select an action, show its exact target, then explicitly confirm destructive operations. For example:

```text
CLEAR HAT · PATTERN 2
SAVE: CONFIRM   HOME: CANCEL
```

Existing confirmations are useful, but messages such as `UNSAVED: SAVE AGAIN` during loading should explain the consequence: `DISCARD CHANGES AND LOAD 4?`.

## 7. Give performance information priority over setup information

LIVE is two HOME presses away, while the scope gets the first press. Make the HOME destination configurable, with LIVE as the performance-oriented default.

Show tempo, transport state, active pattern, and queued changes clearly—such as `P2 → P3 NEXT BAR`. Keep knob assignment controls separate from the normal playing view.

Undo also deserves a quicker, documented shortcut; it is currently buried in PATCH.

## 8. Refine the shared screen layout

The large value popups obscure the waveform, filter response, or drum steps precisely while you are adjusting them. Enlarge or highlight the value in its existing knob cell instead, reserving overlays for confirmations and exceptional messages.

Increase contrast for meaningful labels, give long values a consistent compact format, and reserve a fixed header area for status. The CPU meter could move to SETUP during normal operation, freeing space for musical context.

## Suggested implementation order

The first implementation pass should address SELECT behaviour, LIVE assignment mode, inactive controls, popup placement, and consistent labels. Those changes should improve everyday use with relatively little disruption.

Oscillator regrouping and sequencer navigation deserve a second pass, tested through actual tasks such as building a patch, entering a pattern, and switching parts while playing.
