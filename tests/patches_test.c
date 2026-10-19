/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patch bank on a computer: a format 1 bank (before the LFO) converts to format 2, keeping every sound
 * and giving the LFO no amount; format 2 converts too (the effects off); the format saves and loads back.
 *   tests/run_engine.sh runs it */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/engine.c"

static uint8_t store[4096];                     /* a fake store (platform.c's store_load / store_save) */
static uint32_t store_n;
static int store_load(void *p, uint32_t n) { if (n != store_n) return -1; memcpy(p, store, n); return 0; }
static int store_save(const void *p, uint32_t n) { memcpy(store, p, n); store_n = n; return 0; }
#include "../firmware/src/patches.c"

static int fails;
#define CHECK(c, ...) do { printf("  %s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)

int main(void)
{
    struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V1_SIZE]; } v1;
    patch_t a = PATCH_INIT, b = PATCH_INIT, got;
    uint32_t s;
    printf("patch bank checks\n");
    a.osc[0].bank[0] = 3; a.osc[1].pos[2] = 99; a.cut = 12; a.fine2 = 77;   /* two sounds, saved by format 1 */
    b.osc[0].amt[1] = 120; b.amp_r = 5; b.semi2 = 31;
    memset(&v1, 0, sizeof v1);
    v1.version = 1;
    v1.used[2] = 1; memcpy(v1.p[2], &a, PATCH_V1_SIZE);
    v1.used[7] = 1; memcpy(v1.p[7], &b, PATCH_V1_SIZE);
    store_save(&v1, sizeof v1);
    patches_load();
    CHECK(bank.version == PATCH_VERSION, "the bank is format %u after loading format 1", (unsigned)bank.version);
    for (s = 0; s < PATCH_SLOTS; s++)
        if (s != 2 && s != 7 && bank.used[s]) break;
    CHECK(s == PATCH_SLOTS && bank.used[2] && bank.used[7], "slots 3 and 8 used, the others empty");
    CHECK(patch_get(2, &got) == 0 && !memcmp(&got, &a, PATCH_V1_SIZE), "slot 3's sound kept");
    CHECK(patch_get(7, &got) == 0 && !memcmp(&got, &b, PATCH_V1_SIZE), "slot 8's sound kept");
    CHECK(got.lfo_amt[0] == 64 && got.lfo_amt[1] == 64 && got.lfo_rate == PATCH_INIT.lfo_rate,
          "the LFO: no amount, the default rate");
    got.lfo_amt[0] = 100; got.lfo_dest[0] = DEST_CUTOFF;
    CHECK(patch_save(4, &got) == 0 && store_n == sizeof bank, "a save writes format %u (%u bytes)", (unsigned)PATCH_VERSION, (unsigned)store_n);
    memset(&bank, 0, sizeof bank);
    patches_load();
    CHECK(patch_get(4, &a) == 0 && !memcmp(&a, &got, sizeof a), "the new format loads back as it was saved");
    CHECK(patch_get(2, &a) == 0 && patch_get(7, &a) == 0, "the converted slots stay");
    {                                           /* format 2 (with the LFO, before the effects) -> format 3 */
        struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V2_SIZE]; } v2;
        patch_t c = PATCH_INIT;
        c.lfo_dest[1] = DEST_MWAVE2; c.lfo_amt[1] = 20; c.osc[1].ratio[2] = 9;
        memset(&v2, 0, sizeof v2);
        v2.version = 2;
        v2.used[5] = 1; memcpy(v2.p[5], &c, PATCH_V2_SIZE);
        store_save(&v2, sizeof v2);
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(5, &a) == 0 && !memcmp(&a, &c, PATCH_V2_SIZE) && bank.version == PATCH_VERSION,
              "format 2: slot 6's sound and LFO kept");
        CHECK(a.rev_mix == 0 && a.dly_mix == 0 && a.cho_mix == 0, "format 2: the effects come in off");
    }
    {                                           /* format 3 (with the effects, before the mod envelope) -> 4 */
        struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V3_SIZE]; } v3;
        patch_t c = PATCH_INIT;
        c.rev_mix = 90; c.dly_shift = 20; c.lfo_rate = 3;
        memset(&v3, 0, sizeof v3);
        v3.version = 3;
        v3.used[0] = 1; memcpy(v3.p[0], &c, PATCH_V3_SIZE);
        store_save(&v3, sizeof v3);
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(0, &a) == 0 && !memcmp(&a, &c, PATCH_V3_SIZE) && bank.version == PATCH_VERSION,
              "format 3: slot 1's sound and effects kept");
        CHECK(a.menv_amt == 64, "format 3: the mod envelope comes in with no amount");
    }
    {                                           /* format 4 (with the mod envelope, before velocity) -> 5 */
        struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V4_SIZE]; } v4;
        patch_t c = PATCH_INIT;
        c.menv_loop = 1; c.menv_amt = 100; c.rev_shimmer = 77;
        memset(&v4, 0, sizeof v4);
        v4.version = 4;
        v4.used[3] = 1; memcpy(v4.p[3], &c, PATCH_V4_SIZE);
        store_save(&v4, sizeof v4);
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(3, &a) == 0 && !memcmp(&a, &c, PATCH_V4_SIZE) && bank.version == PATCH_VERSION,
              "format 4: slot 4's sound and mod envelope kept");
        CHECK(a.vel_amp == PATCH_INIT.vel_amp, "format 4: velocity comes in as the default sound has it");
    }
    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
