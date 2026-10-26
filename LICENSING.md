# Licensing

Prysm is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| Prysm: the sound engine `firmware/src/engine.c`, the effects `fx.c`, the pages `ui.c`, the patches `patches.c`, the app `prysm.c` (from Dinghy's `dinghy.c`), the USB audio layout, the store and the request log in `usb.c` / `platform.c`, `tools/gen_prysm_tables.py`, `tools/gen_splash.py`, `tools/make_site.py`, `tools/usblog.py`, `tools/setup_macos.sh`, `tests/engine_test.c`, `tests/check_engine.py`, `tests/patches_test.c`, `tests/run_ui.sh`, `web/index.html`, the docs `README.md`, `SETUP-MACOS.md`, `docs/MANUAL.md`, `docs/PRYSM-DESIGN.md` | Copyright (C) 2026 Nicholas Pratt |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` (the build, the package, the installer); the web installer's update protocol `web/fm1ota.js`, `web/fm1pkg.js` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit) |
| The TRS MIDI IN `firmware/src/midi_uart.c` | Felucca's, as sloopDX carries it, with the ring read by content from Felucca [Salt] 0.9-salt14 by ChanceTheMaker (found by keremimo) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (from OberVave, the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| From OberVave: the storage (blobs, slots), `LCD_IDLE`, the SMK-37 wiring of `platform.c`, `store.h`, the pad event queue in `smk_input.h`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, the CI workflow | Copyright (C) 2026 zvenson |
| Dinghy, which Prysm started from: the app, the wiring `platform.c`, the host mode `tests/host.c`, the platform docs `docs/API.md`, `docs/RULES.md`, `docs/MAKE-IT-YOURS.md` | zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |

The reverb follows the plate in Jon Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45(9),
1997 (an algorithm, implemented here).

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt`, <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| The splash picture `assets/splash.jpg` (and `firmware/gen/prysm_splash.h`, made from it), supplied by Nicholas Pratt | its author's: not covered by the GPL | |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their owners.
Prysm is not made or endorsed by them, nor by the authors of Dinghy or SLOOP.
