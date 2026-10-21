# Prysm's tools

Run every command from the `Prysm` folder, in Terminal, with the project's Python:

```
cd /Users/nicholas/Documents/code/fm-1/Prysm
```

Paste only the command line itself, not the `%` prompt or any output.

`PY` below stands for the project's Python, `/Users/nicholas/Documents/code/fm-1/.venv/bin/python3`. Either type it
out in full, or set it once per Terminal window:

```
PY=/Users/nicholas/Documents/code/fm-1/.venv/bin/python3
```

and then write `$PY` in each command, as below.

---

## Install firmware on the FM-1

Connect the FM-1 by USB, then:

```
$PY tools/fm1_install.py build/prysm.fwsc
```

It asks before it writes, then the FM-1 restarts into the new firmware. To install a release instead, give its
file: `$PY tools/fm1_install.py prysm-0.8.fwsc`. To see what the connected FM-1 is running:
`$PY tools/fm1_install.py --info`.

If the FM-1 does not start after an update, hold **OCT-** while switching it on to get the rescue updater, then
install again.

---

## Film a performance (`capture.py`)

This makes a video of you playing the FM-1. The video shows the screen exactly as the FM-1 draws it, with the
sound perfectly in sync. It is not filmed with a camera: the FM-1 sends everything you do to the computer, which
plays the take again through Prysm's own code and draws every frame.

You need firmware with capture: 0.8 or later, or a build made after it was added.

### Record a take

1. Connect the FM-1 by USB.
2. On the FM-1, set things up the way the video should start: the sound and the page you want on screen. Hold no
   keys.
3. In Terminal:

   ```
   $PY tools/capture.py record mytake
   ```

   Use any name in place of `mytake`. Leave it out and the tool names the take by the date and time.
4. Press **Enter** to start recording, then play.
5. Press **Enter** again to stop.

The tool then plays the take again and writes the video. That takes a few seconds per second of video.

### What you get

| File | What it is |
| --- | --- |
| `takes/mytake.mp4` | the video: 720 x 720, 30 frames a second, with the sound |
| `takes/mytake/audio.wav` | the sound alone, for an editor |
| `takes/mytake/state.bin`, `events.txt` | the take itself, so you can make the video again |

The `takes/` folder is not part of the project's git history.

### Options

Add these to `record` or `render`:

| Option | Does |
| --- | --- |
| `--fps 60` | 60 frames a second (default 30) |
| `--size 1080` | the video's width and height in pixels (default 720). The screen's pixels stay sharp at any size. |
| `--tail 5` | seconds of video after the last thing you played, for the release and the reverb (default 3) |
| `--splash` | start the video with the power-on animation |

For example, to record at 60 frames a second in 1080 x 1080, starting with the animation:

```
$PY tools/capture.py record mytake --fps 60 --size 1080 --splash
```

### Make the video of a take again

To change the options without playing again:

```
$PY tools/capture.py render takes/mytake --size 1080 --tail 6
```

### What it records, and what it does not

Recorded:
- the 27 keys, every button and every knob, including SELECT, ALGORITHM and PRESETS
- MIDI arriving over USB (a keyboard or a DAW playing the FM-1, pitch bend)
- what the FM-1 starts from: the 8 patch slots, the current sound (unsaved changes included), the settings, the
  page and row on screen, and whether the sequencer is running

Not recorded:
- the **MASTER** volume knob: the video is at full level
- the **MIDI clock**: a take played with CLOCK = MIDI plays back at the FM-1's own tempo

A random LFO (S&H) or the arpeggiator's RAND mode may come out slightly differently from what you heard while
playing. Everything else is the same.

### If something goes wrong

- **"no FM-1 among the MIDI ports"**: the FM-1 is not connected, or another app has taken its MIDI port. Unplug it
  and plug it in again, or close the other app.
- **"the FM-1 sent no take"**: the FM-1 runs firmware without capture. Install a newer build (above).
- **"no ffmpeg"**: run `/Users/nicholas/Documents/code/fm-1/.venv/bin/pip install imageio-ffmpeg`.

---

## Build and release (for development)

| Command | Does |
| --- | --- |
| `tools/setup_macos.sh` | once: sets the Mac up to build Prysm (colima and docker, the JieLi toolchain, the AC79 SDK, the Python venv) |
| `./build.sh` | builds `build/prysm.fwsc` (identity FM-1_980). colima must be running: `colima start`. |
| `./build.sh --release 0.8` | a release build, `build/prysm-0.8.fwsc` |
| `$PY tools/make_site.py build/prysm-0.8.fwsc 0.8` | rebuilds `site/`: the web installer and the manual for GitHub Pages |

## The other tools

| Tool | What it is for |
| --- | --- |
| `build.py`, `fm1pkg_make.py`, `lz4blk.py`, `get_toolchain.sh` | used by `./build.sh`: the build, the update package, its compression, the JieLi toolchain download |
| `gen_prysm_tables.py` | regenerates `firmware/gen/prysm_tables.h`: the wavetables and the engine's lookup tables (needs numpy) |
| `gen_prysm_font.py` | regenerates the screen's large fonts from Terminus BDF files: `gen_prysm_font.py ter-u22b.bdf M` (the values) and `gen_prysm_font.py ter-u32b.bdf P` (the pop-up) |
| `gen_splash.py` | the old power-on picture, `assets/splash.jpg`. No longer shown: the power-on animation replaced it. |
| `demo.py` | the demo video: `send` puts its 7 sounds in the FM-1's slots (0.8 or later), `film` scripts and films its scenes on the computer |
| `../emu/build.sh` | Prysm in a browser and the editor: `site/editor/` (needs emscripten: `~/emsdk`, `source ~/emsdk/emsdk_env.sh`) |
| `usblog.py` | debugging only: reads a USB log from firmware built with `DINGHY_USB_LOG` |
