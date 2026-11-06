/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Nicholas Pratt */
/* The FM-1's flash chips (hal/fm1_flash_map.h FL_JEDEC_OK, used by the app, the USB rescue and the update loader):
 * the Puya P25Q80 (0x856014) of the first units and the UC25HQ80 (0xB36014) of newer ones are accepted; nothing else
 * (no chip / a dead bus reads 0x000000 or 0xFFFFFF). Without the UC25HQ80 a newer FM-1 refuses the install and,
 * with Prysm on it, saves nothing and takes no update. */
#include <stdint.h>
#include <stdio.h>
#include "fm1_flash_map.h"
#if BOARD_SMK37
#error "the FM-1 build (no -DBOARD_SMK37)"
#endif

int main(void)
{
    int ok = FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) &&
             !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0xEB6014u) && !FL_JEDEC_OK(0xB36015u);
    printf("FM-1 flash ids: P25Q80 0x856014 and UC25HQ80 0xB36014 only  %s\n", ok ? "ok" : "FAIL");
    return !ok;
}
