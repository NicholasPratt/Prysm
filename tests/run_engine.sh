#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Prysm's engine on this computer: render the test patches (build/engine/*.wav), check them; then the patch bank;
# then REC (the sequencer recording, through the app's code); then each slot's own sequencer; then the LIVE screen; then the split and its parts;
# then the hat's ratchets; then REC on the drums; then the drums' copy and paste.
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
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/seqslot_test tests/seqslot_test.c
build/engine/seqslot_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/live_test tests/live_test.c
build/engine/live_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/split_test tests/split_test.c
build/engine/split_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/roll_test tests/roll_test.c
build/engine/roll_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/drumrec_test tests/drumrec_test.c
build/engine/drumrec_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/drumcopy_test tests/drumcopy_test.c
build/engine/drumcopy_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/feat_test tests/feat_test.c
build/engine/feat_test | grep -v ' ms  '
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/engine/drummod_test tests/drummod_test.c
build/engine/drummod_test | grep -v ' ms  '
