#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The two-part demo sounds (tools/demos.c): patches/*.prysm, and build/demos/*.wav to listen to
set -e
cd "$(dirname "$0")/.."
mkdir -p build/demos
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"dev\"" -Ifirmware/src -Ifirmware/gen -o build/demos/demos tools/demos.c
build/demos/demos | grep -v ' ms  '
