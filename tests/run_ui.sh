#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Screenshots of every page and row (tests/ui/pages.txt) -> build/ui/*.png, and one contact sheet build/ui/pages.png
set -e
cd "$(dirname "$0")/.."
mkdir -p build/ui
rm -f build/ui/*.ppm build/ui/*.png
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/ui/prysm firmware/src/prysm.c
build/ui/prysm build/ui/ui tests/ui/pages.txt >/dev/null
python3 - <<'PY'
from pathlib import Path
from PIL import Image
shots = sorted(Path("build/ui").glob("ui-*.ppm"), key=lambda p: int(p.stem.split("-")[1]))
sheet = Image.new("RGB", (4 * 250, ((len(shots) + 3) // 4) * 250), (40, 40, 40))
for i, p in enumerate(shots):
    im = Image.open(p)
    im.save(p.with_suffix(".png"))
    sheet.paste(im, ((i % 4) * 250 + 5, (i // 4) * 250 + 5))
    p.unlink()
sheet.save("build/ui/pages.png")
print(f"{len(shots)} screens -> build/ui/pages.png")
PY
