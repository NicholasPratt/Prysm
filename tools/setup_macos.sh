#!/bin/sh
# Set up a Mac to build Prysm (see SETUP-MACOS.md). Safe to run again: each step skips what is already there.
#   tools/setup_macos.sh
set -e
cd "$(dirname "$0")/.."

command -v brew >/dev/null || { echo "setup: install Homebrew first (https://brew.sh)"; exit 1; }
for f in colima docker docker-buildx; do brew list "$f" >/dev/null 2>&1 || brew install "$f"; done
colima status >/dev/null 2>&1 || colima start --cpu 4 --memory 6 --disk 40

[ -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ] || tools/get_toolchain.sh

SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
[ -f "$SDK/cpu/wl82/tools/uboot.boot" ] || git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$SDK"

[ -x .venv/bin/python3 ] || python3 -m venv .venv
.venv/bin/pip install -q Pillow fonttools mido python-rtmidi

echo "setup: done. Before building: export PATH=\"$PWD/.venv/bin:\$PATH\" (and colima start after a reboot)"
