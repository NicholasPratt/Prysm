#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prysm's engine on this computer: render the test patches (build/engine/*.wav), check them; then the patch bank;
# then REC (the sequencer recording, through the app's code).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/engine
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable -Ifirmware/gen -o build/engine/engine_test tests/engine_test.c
build/engine/engine_test
python3 tests/check_engine.py
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable -Ifirmware/gen -o build/engine/patches_test tests/patches_test.c
build/engine/patches_test
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/rec_test tests/rec_test.c
build/engine/rec_test
