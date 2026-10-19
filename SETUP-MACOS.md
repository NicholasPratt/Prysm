# Prysm on macOS

Prysm is a synth-module firmware for the M-VAVE FM-1, built on [Dinghy](https://github.com/zvenson/dinghy)
(itself based on [Felucca](https://github.com/hugelton/Felucca) and [SLOOP](https://github.com/isod89/sloop-fm1)).
This page covers building it on a Mac; [README.md](README.md) covers the platform itself.

## Set up once

```sh
tools/setup_macos.sh
```

This installs and checks the following:

| What | Where |
| --- | --- |
| colima + docker (Homebrew): the JieLi toolchain is Linux-only and runs in a `linux/amd64` container | `colima start` after a reboot |
| JieLi pi32v2 toolchain (clang 4.0.1) | `~/.jieli/toolchain` |
| JieLi AC79 SDK v1.2.1 (three files of it are used) | `~/fw-AC79_AIoT_SDK` |
| Python venv: Pillow, fonttools, mido, python-rtmidi | `.venv/` (git-ignored) |

## The loop

```sh
export PATH="$PWD/.venv/bin:$PATH"
tests/run_host.sh                                  # on the Mac: build/host/dinghy.wav and screen PNGs
./build.sh                                         # build/dinghy.fwsc
python3 tools/fm1_install.py build/dinghy.fwsc     # onto the FM-1 over USB-MIDI
python3 tools/fm1_install.py --info                # what the FM-1 runs
```

Coming from ETCH, the install is refused: ETCH only accepts its own update loader or the official V15
firmware. Restore the official V15 first (ETCH V1.38's web installer, *RESTORE - OFFICIAL V15*), then install.

Rescue: hold **OCT-** while switching the FM-1 on to get the updater with no app running.

## USB audio on macOS 13 / 14

The FM-1 shows up as a 2-in / 0-out audio device (the master output, 16-bit stereo, 44.1 or 48 kHz).

Felucca's USB audio layout (MIDI interface 1, audio streaming interface 2, line in wired straight to the USB
output terminal) is matched by AppleUSBAudio on macOS 13 / 14 but never brought up: the host reads the
descriptors, sets the configuration and sends no audio request (Felucca issue #67). Prysm describes the audio
function the way the stock firmware does, and macOS 14.4 takes it:

- interfaces 0 audio control, 1 audio streaming, 2 MIDI streaming;
- line in (terminal 1) -> feature unit 3 (master mute + volume) -> USB streaming (terminal 2);
- bcdDevice 3.2a, so a Mac never reuses descriptors it cached for an older layout.

The feature unit's mute and volume are answered and stored, not yet applied to the stream. See the comments in
`firmware/src/usb.c`.

## Debug: what the host asked over USB

Builds keep a log of the USB setup requests the host sends (`DINGHY_USB_LOG` in `firmware/src/usb.c`, off in the
update loader). Read it back over MIDI:

```sh
python3 tools/usblog.py
```

Each request is decoded (standard, audio class) with whether the FM-1 stalled it. Set `DINGHY_USB_LOG` to 0 for
a release build.

## Licence

GPL-3.0, as Dinghy and Felucca: see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md). Firmware you
distribute must come with its source.
