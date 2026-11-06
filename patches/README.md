# Demo sounds

Two-part sounds for Prysm 1.5, each made from two of the factory tones. Open one with **Load patch** in the
[editor](https://nicholaspratt.github.io/Prysm/editor/) (or the plugin's window); with an FM-1 connected, **Store on
FM-1** saves it in a slot.

| File | Mode | Part A | Part B |
| --- | --- | --- | --- |
| 1-split-bass-pad | SPLIT at C4 | FM BASS (an octave down) | WARM PAD |
| 2-layer-epiano-strings | LAYER | E.PIANO, a little left | STRINGS, a little right |
| 3-layer-bell-glass | LAYER | DX BELL, left | GLASS PAD, right |
| 4-dual-bass-lead | DUAL | PLUCKBASS | SAW LEAD, to the right |
| 5-split-cello-brass | SPLIT at C4 | CELLO | BRASS |
| 6-layer-choir-chimes | LAYER | AAH CHOIR | CHIMES, right |
| 7-dual-acid-drone | DUAL | ACID | DARKDRONE |
| 8-layer-kalimba-space | LAYER | KALIMBA, left | SPACE, right |

In DUAL the keys play the part you are editing (SELECT switches it), so play the bass on A, switch, and play the lead
on B; the sequencer plays each note on its own part (SEQ's PART row).

Made by `tools/make_demos.sh` (tools/demos.c), which also plays each into `build/demos/` to listen to.
