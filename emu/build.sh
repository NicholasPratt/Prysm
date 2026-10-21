#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prysm in a browser: firmware/src/prysm.c compiled to WebAssembly (emu/wasm.c: the FM-1, emu/exports.c: what the
# page calls), with the editor page (web/editor/) -> site/editor/ (GitHub Pages: nicholaspratt.github.io/Prysm/editor/).
# Needs emscripten (brew install emscripten). Try it: python3 -m http.server -d site, then localhost:8000/editor/
set -e
cd "$(dirname "$0")/.."
OUT=site/editor
mkdir -p "$OUT"
emcc -O2 -w -DDINGHY_HOST -DDINGHY_WEB -DAPP_VER="\"${APP_VER:-$(git describe --tags --always)}\"" \
    -Ifirmware/src -Ifirmware/gen firmware/src/prysm.c -o "$OUT/prysm.js" \
    -sASYNCIFY -sMODULARIZE -sEXPORT_NAME=Prysm -sINVOKE_RUN=0 -sENVIRONMENT=web -sINITIAL_MEMORY=16MB \
    -sEXPORTED_FUNCTIONS=_main -sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAPU16,UTF8ToString
cp web/editor/index.html web/editor/editor.js web/editor/worklet.js "$OUT/"
ls -l "$OUT"
