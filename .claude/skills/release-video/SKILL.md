---
name: release-video
description: Make a Prysm release or feature video (a "what's new" film) from start to YouTube upload. Use when asked for a release video, a what's-new film, a demo film of new features, or a thumbnail / title / description / tags for one. Covers the film script in tools/films/, rendering, checking the mix and frames, the thumbnail, and the upload text.
---

# Prysm release video

A film is a Python script in `tools/films/` that drives the firmware itself (compiled for this computer) through scenes:
button presses, knob turns, captions, and patterns playing. `tools/film.py` renders each scene's screen and sound and
joins them into one video with subtitles and chapters. Nothing is filmed from the hardware.

Run everything from `Prysm/` with the project's Python, `.venv/bin/python`. The system `python3` lacks PIL and numpy.
`ffmpeg` must be on the PATH.

## Ground rules

- **Never modify `tools/film.py`** or any older `tools/films/whatsnew*.py`. They may hold the user's uncommitted work.
  Reading them is fine. When film.py is out of date with the firmware (new pages, rows, stepped knobs), patch its
  tables at load time from the new film script, as `whatsnew17.py` does (`_fw_tables()`, the `Scene` subclass).
- Commit and push only when the user asks. `takes/` is output and is not committed.
- Build sounds with knob settings (`knobs()`, using the firmware's own knob table) rather than raw byte offsets, so the
  offsets can't drift. `whatsnew17.py --knobs` prints the table: (page, row, knob index 0-3[, "alt"]) -> byte offset,
  max, min, kind, label.

## 1. Plan

Ask nothing that the release notes answer. Read `git log`, `docs/MANUAL.md` and the release notes for the version.
Then pick:

- **The music.** One track carries the whole film, so the features show up as changes to a sound you're already
  hearing. Set tempo and key up front (whatsnew17: 140 BPM techno in F minor, DUAL with part A a bass and part B a lead).
- **One scene per feature,** in an order that builds. Each scene picks up the sound where the last one left it
  (`after=S[-1]`, `slots=one(S[-1].patch, seq)`), and its end state is also recorded in a `*_DONE` dict so later
  scenes can be rendered alone.
- **The story the captions tell.** Captions only, no voice. Titles are short claims; bodies say which buttons and knobs,
  the way a player would do it.
- **An ending** that contrasts with the rest (whatsnew17: chaos, then a quiet chorus pad and the install link).

## 2. Write the film

Copy `tools/films/whatsnew17.py` as the template; it is the most complete. Its parts:

- `_fw_tables()` plus the monkeypatching of `F.PAGES`, `F.ROWS`, `F.PG` and `F.STEPPED`: film.py brought up to date.
- `class Scene(F.Scene)`: `press` and `go` for current page cycles, and `pin(row, col, level)` for the ROUTE grid.
- `cpatch(base, C lines)` and `knobs(base, {(page, row, knob 1-4[, "alt"]): value, ("PIN", row, col): level})`.
- `Seq` from film.py: `.note(pat, step, note, lane, steps, accent, slide, part)`, `.drum(pat, d, steps, v)`,
  `.raw("C;")` for anything else (drum levels, rolls, pattern length, `seqs.cur`).
- `Clock(s, bpm)`: `until(bar)`, `hold(extra=n)` (the caption read, then on to the next bar line).
- `scenes()` returning the list, and `film(scenes(), NAME, footer, only=sys.argv[1:], ver=VERSION)`.

## 3. Render

    PATH="$PWD/.venv/bin:$PATH" .venv/bin/python tools/films/NAME.py            # all scenes, then joined
    PATH="$PWD/.venv/bin:$PATH" .venv/bin/python tools/films/NAME.py 03-comp    # one scene again, then joined

Output goes to `takes/NAME/`:
- `NAME.mp4`, `NAME.srt`, `chapters.txt`
- per scene: `SCENE.mp4` (the screen alone, 960x960), `SCENE/framed.mp4` (with the captions), and `SCENE/audio.wav`
  (the raw sound, before any normalising)

Render one scene first, and check it, before rendering all of them.

## 4. Check it (before showing the user)

**Frames.** Make a contact sheet and look at it:

    ffmpeg -loglevel error -y -i takes/NAME/SCENE/framed.mp4 -vf "fps=1/5,scale=640:-1,tile=3x3" -frames:v 1 sheet.png

Check that the cursor lands on the right rows, pins and knobs, that values read as intended, and that captions match
what's on screen.

**Mix.** Measure, don't assume:
- RMS every 2-3 s of each `audio.wav`, for dips and jumps between scenes.
- **Every part audible:** render the scene with one part muted (`("MIX", "LEVELS", 1)` or `2` set to 0, or the drum
  levels set to 0 with `.raw`) and compare it with the full mix, overall and by band (20-120, 120-400, 400-2000,
  2000-10000 Hz). If muting a part changes the mix by less than about 1 dB, nobody can hear it.
- **Peaks pinned at 0 dB** mean the safety limiter is working all the time: it squeezes out sustained parts (bass)
  under the kick. Check the compressor's make-up GAIN.
- **Bass:** a low filter cutoff with notes below F2 is all sub, inaudible on laptops and phones. Give it low-mid
  energy (cutoff about 64, a sustaining amp envelope).
- Watch for knob values past a knob's max (the table's max column), such as SYNC rates above 127.

Render scratch experiments into their own take names (e.g. `takes/basstest`) and delete them afterwards.

Report measurements as measurements. You can't listen, so say that the user should listen for balance.

## 5. Thumbnail

    .venv/bin/python tools/films/thumbnail.py takes/NAME/SCENE.mp4 SECONDS "HEADLINE." \
        --kicker "PRYSM X.Y" --sub "free firmware for the M-VAVE FM-1" -o takes/NAME/thumbnail.png

- Use a scene's raw take (`SCENE.mp4`, not `framed.mp4`) at its busiest, most colourful moment. The scope at full
  noise works well, at the default `--crop` (the trace band). Use `--crop full` for the whole screen, such as the
  ROUTE grid.
- Headline: two to five words, the film's one claim, in capitals (whatsnew17: "NOT FOR BEGINNERS.").
- Open the PNG and look at it: is the headline readable small, and is the picture behind it busy without fighting it?

## 6. Upload text

Write `takes/NAME/youtube.txt` and also give the text in the reply, ready to paste. `takes/whatsnew17/youtube.txt`
is the model. It holds:

- **Title** (100 characters at most; aim for about 70): "Prysm X.Y – <what you hear> on an M-VAVE FM-1 (<the claim>)".
- **Description:**
  1. A two-line hook.
  2. Links: the installer https://nicholaspratt.github.io/Prysm/, the release page
     `https://github.com/NicholasPratt/Prysm/releases/tag/vX.Y`, and the manual.
  3. "NEW IN X.Y" bullets, taken from the release notes; claim nothing the notes don't.
  4. Whether saved sounds carry over.
  5. **CHAPTERS** from the final `takes/NAME/chapters.txt`, with the scene numbers dropped. The first must be 0:00,
     and YouTube needs at least three chapters, each 10 s or longer.
  6. Where the sound comes from: Prysm's own engine, the screen being the synth's own display. Don't claim it was
     recorded from the hardware.
  7. The line "independent project, not affiliated with M-VAVE / Cuvave, custom firmware at your own risk".
  8. Three hashtags.
- **Tags** (500 characters at most, comma-separated): Prysm, M-VAVE FM-1, MVAVE FM1, FM-1 firmware, custom firmware,
  open source synth, FM synthesis, plus the genre and that video's features.
- **Category** (Music), **subtitles** (`NAME.srt`, English), **thumbnail** (`thumbnail.png`).

Before handing over the text, flag anything in it that the released firmware doesn't do yet. For example, a fix that
is committed but not released.
