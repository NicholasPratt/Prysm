#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Screenshots of every page and row (tests/ui/pages.txt), of the PATCH screen (tests/ui/patches.txt), of MIX's sends (tests/ui/sends.txt), of the noise
# and the drums (tests/ui/drums.txt), of the LIVE screen (tests/ui/live.txt), of 1.4's pages (tests/ui/new.txt), of RANDOM (tests/ui/random.txt), of TRANS (tests/ui/trans.txt), of the parts (tests/ui/parts.txt), of TONE (tests/ui/tones.txt), of the parts side by side (tests/ui/levels.txt)
# -> build/ui/*.png, and contact sheets build/ui/pages.png, build/ui/patches.png
set -e
cd "$(dirname "$0")/.."
mkdir -p build/ui
rm -f build/ui/*.ppm build/ui/*.png
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/ui/prysm firmware/src/prysm.c
build/ui/prysm build/ui/ui tests/ui/pages.txt >/dev/null
build/ui/prysm build/ui/pa tests/ui/patches.txt >/dev/null
build/ui/prysm build/ui/screen tests/ui/screen.txt >/dev/null   # docs/screen.png (the hub's picture), below
build/ui/prysm build/ui/sc tests/ui/scope.txt >/dev/null
build/ui/prysm build/ui/ar tests/ui/arp.txt >/dev/null
build/ui/prysm build/ui/su tests/ui/setup.txt >/dev/null
build/ui/prysm build/ui/pr tests/ui/presets.txt >/dev/null
build/ui/prysm build/ui/sq tests/ui/seq.txt >/dev/null
build/ui/prysm build/ui/en tests/ui/env.txt >/dev/null
build/ui/prysm build/ui/dr tests/ui/drums.txt >/dev/null
build/ui/prysm build/ui/lv tests/ui/live.txt >/dev/null
build/ui/prysm build/ui/nw tests/ui/new.txt >/dev/null
build/ui/prysm build/ui/rn tests/ui/random.txt >/dev/null
build/ui/prysm build/ui/tr tests/ui/trans.txt >/dev/null
build/ui/prysm build/ui/pt tests/ui/parts.txt >/dev/null
build/ui/prysm build/ui/tn tests/ui/tones.txt >/dev/null
build/ui/prysm build/ui/lp tests/ui/levels.txt >/dev/null
build/ui/prysm build/ui/rt tests/ui/sends.txt >/dev/null
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for prefix, sheet_name in (("ui", "pages"), ("pa", "patches"), ("sc", "scope"), ("ar", "arp"), ("su", "setup"), ("pr", "presets"), ("sq", "seq"), ("en", "env"), ("dr", "drums"), ("lv", "live"), ("nw", "new"), ("rn", "random"), ("tr", "trans"), ("pt", "parts"), ("tn", "tones"), ("lp", "levels"), ("rt", "sends")):
    shots = sorted(Path("build/ui").glob(prefix + "-*.ppm"), key=lambda p: int(p.stem.split("-")[1]))
    sheet = Image.new("RGB", (4 * 250, ((len(shots) + 3) // 4) * 250), (40, 40, 40))
    for i, p in enumerate(shots):
        im = Image.open(p)
        im.save(p.with_suffix(".png"))
        sheet.paste(im, ((i % 4) * 250 + 5, (i // 4) * 250 + 5))
        p.unlink()
    sheet.save(f"build/ui/{sheet_name}.png")
    print(f"{len(shots)} screens -> build/ui/{sheet_name}.png")
Image.open("build/ui/screen-700.ppm").resize((480, 480), Image.NEAREST).save("docs/screen.png")
Path("build/ui/screen-700.ppm").unlink()
print("docs/screen.png")
PY
