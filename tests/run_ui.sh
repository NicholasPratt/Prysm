#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Screenshots of every page and row (tests/ui/pages.txt) and of the PATCH screen (tests/ui/patches.txt)
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
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for prefix, sheet_name in (("ui", "pages"), ("pa", "patches"), ("sc", "scope")):
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
