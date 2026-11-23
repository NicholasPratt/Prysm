#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A YouTube thumbnail (1280 x 720 PNG) for a film: one frame of the FM-1's screen, from a scene's raw take
(takes/NAME/SCENE.mp4, the screen alone), blown up behind a headline.

    tools/films/thumbnail.py takes/whatsnew17/07-chaos.mp4 36 "NOT FOR BEGINNERS." \\
        --kicker "PRYSM 1.7" --sub "free firmware for the M-VAVE FM-1" -o takes/whatsnew17/thumbnail.png

    at        seconds into the take (pick a busy, colourful frame: the scope at full noise, the ROUTE grid)
    --crop    x,y,w,h of the take's frame to keep (default: the scope's trace band; --crop full for the whole screen)
    --dim     how dark the picture behind the text is, 0 .. 1 (default 0.35)

The headline is drawn as large as fits two lines; keep it to two to five words."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import film as F

W, H = 1280, 720
RAINBOW = [(255, 64, 64), (255, 160, 0), (255, 230, 0), (60, 220, 90), (40, 170, 255), (150, 90, 255)]


def frame(video, at):
    with tempfile.TemporaryDirectory() as t:
        p = Path(t) / "f.png"
        subprocess.run([F.ffmpeg_exe(), "-loglevel", "error", "-y", "-ss", str(at), "-i", str(video), "-frames:v", "1",
                        str(p)], check=True)
        return Image.open(p).convert("RGB").copy()


def fit(d, text, bold, width, height):
    """The largest font size at which every line of text fits width, all lines in height."""
    lines = text.split("\n")
    for size in range(220, 20, -4):
        f = F._font(size, bold)
        boxes = [d.textbbox((0, 0), ln, font=f) for ln in lines]
        if max(b[2] - b[0] for b in boxes) <= width and size * 1.02 * len(lines) <= height:
            return f, size
    return F._font(24, bold), 24


def wrap_headline(d, text, width):
    """One line if it fits at a big size, else split into two near-equal lines."""
    words = text.split()
    if len(words) < 3:
        return text
    best = min(range(1, len(words)), key=lambda i: abs(len(" ".join(words[:i])) - len(" ".join(words[i:]))))
    return " ".join(words[:best]) + "\n" + " ".join(words[best:])


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("video")
    a.add_argument("at", type=float)
    a.add_argument("headline")
    a.add_argument("--kicker", default="")
    a.add_argument("--sub", default="")
    a.add_argument("--crop", default="0,150,960,520")
    a.add_argument("--dim", type=float, default=0.35)
    a.add_argument("-o", "--out", required=True)
    o = a.parse_args()

    shot = frame(o.video, o.at)
    if o.crop != "full":
        x, y, w, h = (int(v) for v in o.crop.split(","))
        shot = shot.crop((x, y, x + w, y + h))
    s = max(W / shot.width, H / shot.height)            # (cover the frame; the screen's pixels kept sharp)
    shot = shot.resize((round(shot.width * s), round(shot.height * s)), Image.NEAREST)
    shot = shot.crop(((shot.width - W) // 2, (shot.height - H) // 2, (shot.width - W) // 2 + W, (shot.height - H) // 2 + H))
    img = ImageEnhance.Brightness(shot).enhance(1 - o.dim)
    shade = Image.new("L", (W, H))                      # (darker at the bottom, where the headline sits)
    shade.putdata([min(255, max(0, int((yy / H - 0.35) * 330))) for yy in range(H) for _ in range(W)])
    img = Image.composite(Image.new("RGB", (W, H)), img, shade)
    d = ImageDraw.Draw(img)

    m = 56
    if o.kicker:
        fk = F._font(64, True)
        d.text((m, 40), o.kicker, font=fk, fill="white", stroke_width=4, stroke_fill="black")
        kw = d.textbbox((m, 40), o.kicker, font=fk)[2] - m
        for i, c in enumerate(RAINBOW):                 # (the scope's rainbow, as a rule under it)
            d.rectangle((m + i * kw / 6, 118, m + (i + 1) * kw / 6, 128), fill=c)
    sub_h = 54 if o.sub else 0
    text = wrap_headline(d, o.headline, W - 2 * m)
    fh, size = fit(d, text, True, W - 2 * m, 340)
    n = text.count("\n") + 1
    y = H - m - sub_h - int(size * 1.02 * n)
    d.multiline_text((m - 4, y), text, font=fh, fill="white", spacing=int(size * 0.02) - size // 6,
                     stroke_width=max(4, size // 22), stroke_fill="black")
    if o.sub:
        d.text((m, H - m - 36), o.sub, font=F._font(38, False), fill=(220, 220, 220), stroke_width=3,
               stroke_fill="black")
    Path(o.out).parent.mkdir(parents=True, exist_ok=True)
    img.save(o.out)
    print(f"-> {o.out} ({W} x {H})")


if __name__ == "__main__":
    main()
