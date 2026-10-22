# Prysm, the CLAP plugin

Prysm in your DAW: the FM-1 firmware's own engine makes the sound (the same code, so a project sounds as the FM-1 does),
every knob of the sound is a DAW parameter you can automate, and its window is [the editor](https://nicholaspratt.github.io/Prysm/editor/),
with Prysm's screen, knobs and keys. A connected FM-1 (Prysm 0.9 or later) is kept in step both ways: the project's sound
goes to it, and what you turn on the FM-1 is recorded as the plugin's parameters.

| Parameter | Does |
| --- | --- |
| the sound's 114 | every knob of the pages OSC 1 .. REVERB, named as on the FM-1 (`FILTER CUT`, `OSC 1 RATIO MOD1`) |
| Arp & sequencer on DAW tempo | the arpeggiator, the sequencer and LFO 2's sync follow the DAW's tempo while it plays (otherwise their own BPM) |
| Sequencer run | the sequencer plays (the pattern is part of the sound; edit it in the window's SEQ tab) |
| Plugin sound | the plugin's own sound on or off (off: only the FM-1 plays) |
| Play the FM-1 too | the notes the plugin gets are sent to the FM-1 as well |

The project keeps the whole sound, the sequencer's pattern included.

## Installing

Step by step: [Installing the CLAP plugin](../README.md#installing-the-clap-plugin). In short: download
`Prysm-clap-<your system>.zip` from the [releases](https://github.com/NicholasPratt/Prysm/releases), unzip it, put
`Prysm.clap` in your CLAP folder, then rescan plugins in your DAW.

| System | CLAP folder | Notes |
| --- | --- | --- |
| macOS 10.15+ (Intel or Apple Silicon) | `~/Library/Audio/Plug-Ins/CLAP` | The plugin is not notarized: after unzipping, run `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/Prysm.clap` |
| Windows 10 / 11 | `C:\Program Files\Common Files\CLAP` | Needs the Edge WebView2 runtime (part of Windows 11, and of Windows 10 with current updates). Windows lets only one program open a MIDI port: switch the FM-1 off in the DAW's MIDI devices so the plugin can reach it |
| Linux (x86-64) | `~/.clap` | Needs GTK 3 and WebKitGTK 4.1 (`libwebkit2gtk-4.1-0`) |

**One Prysm per process.** The firmware keeps its state as one FM-1 does, so a second Prysm in the same process stays
silent and says so in its window. Run more of them each in a process of its own (Reaper: the FX window's menu, *Run as*,
*Separate process*). This is not the plan forever.

## Building

```sh
emu/build.sh                                          # site/editor/: the editor page the plugin carries (needs emscripten)
cmake -B build/plugin -S plugin -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugin --config Release           # build/plugin/Prysm.clap
```

Windows: build with clang (`-T ClangCL`), as the firmware is written for GCC and clang. Linux: install `libgtk-3-dev
libwebkit2gtk-4.1-dev libasound2-dev libx11-dev` first. The dependencies (the CLAP SDK, RtMidi, CHOC) are fetched by CMake.
`.github/workflows/plugin.yml` builds all three on every change and attaches them to each release.

| | |
| --- | --- |
| `core/` | the firmware as a library: `firmware/src/prysm.c` built as `tests/host.c` runs it, without its main loop |
| `src/plugin.cpp` | the plugin: parameters, notes, the DAW's tempo as the engine's MIDI clock, 44.1 kHz to the DAW's rate, the window |
| `src/fm1link.cpp` | the FM-1 over USB-MIDI (RtMidi), with the editor's SysEx (`firmware/src/editor.c`) |
| `src/mirror.h` | each watcher of the sound (the window, the FM-1) told only of changes it did not make |
