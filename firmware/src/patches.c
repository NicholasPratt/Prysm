/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patches: 8 slots, kept in the platform's store (store_load / store_save: two flash sectors written in
 * turn, so a save cut short loses only itself). The bank is in RAM; a save writes it whole.
 * The format is checked by its size and PATCH_VERSION: a patch_t that changes shape never reads as wrong values.
 * New fields go at the end of patch_t; the older formats are converted when read (patches_load), with the new
 * fields as PATCH_INIT has them:
 *   1  up to fine2 (55 bytes)
 *   2  + the LFO (lfo_wave .. lfo_amt: 63 bytes)
 *   3  + the effects (cho_rate .. rev_shimmer: 81 bytes)
 *   4  + the mod envelope (menv_a .. menv_amt: 88 bytes)
 *   5  + velocity (vel_amp .. vel_mod: 91 bytes)
 *   6  + glide and the arpeggiator (glide .. arp_swing: 101 bytes)
 *   7  + the sequencer (seq_len .. seq_slide: 297 bytes)
 *   8  + the high-pass and the comb (hp_cut .. comb_mix) */
#define PATCH_SLOTS 8u
#define PATCH_VERSION 8u
#define PATCH_V1_SIZE 55u                       /* format 1's patch_t: everything before lfo_wave */
#define PATCH_V2_SIZE 63u                       /* format 2's: everything before cho_rate */
#define PATCH_V3_SIZE 81u                       /* format 3's: everything before menv_a */
#define PATCH_V4_SIZE 88u                       /* format 4's: everything before vel_amp */
#define PATCH_V5_SIZE 91u                       /* format 5's: everything before glide */
#define PATCH_V6_SIZE 101u                      /* format 6's: everything before seq_len */
#define PATCH_V7_SIZE 297u                      /* format 7's: everything before hp_cut */
#define PATCH_OLD_MAX PATCH_V7_SIZE
_Static_assert(__builtin_offsetof(patch_t, lfo_wave) == PATCH_V1_SIZE, "format 1 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, cho_rate) == PATCH_V2_SIZE, "format 2 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, menv_a) == PATCH_V3_SIZE, "format 3 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, vel_amp) == PATCH_V4_SIZE, "format 4 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, glide) == PATCH_V5_SIZE, "format 5 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, seq_len) == PATCH_V6_SIZE, "format 6 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, hp_cut) == PATCH_V7_SIZE, "format 7 is a prefix of patch_t");

static struct {
    uint32_t version;
    uint8_t used[PATCH_SLOTS];
    patch_t p[PATCH_SLOTS];
} bank;

static int patches_from_old(uint32_t version, uint32_t size)   /* an older bank -> bank; 0 = there was one */
{
    static uint8_t old[4u + PATCH_SLOTS + PATCH_SLOTS * PATCH_OLD_MAX];   /* version, used[], the patches */
    uint32_t s, v;
    if (store_load(old, 4u + PATCH_SLOTS + PATCH_SLOTS * size) != 0)
        return -1;
    memcpy(&v, old, 4);
    if (v != version)
        return -1;
    for (s = 0; s < PATCH_SLOTS; s++) {
        bank.p[s] = PATCH_INIT;                 /* the new fields: as the default sound has them */
        memcpy(&bank.p[s], old + 4u + PATCH_SLOTS + s * size, size);
        bank.used[s] = old[4u + s];
    }
    return 0;
}

static void patches_load(void)                  /* at boot: the saved bank (converted if older), or an empty one */
{
    if (store_load(&bank, sizeof bank) == 0 && bank.version == PATCH_VERSION)
        return;
    memset(&bank, 0, sizeof bank);
    if (patches_from_old(7u, PATCH_V7_SIZE) != 0 &&   /* (kept in RAM; written in the new format by the next save) */
        patches_from_old(6u, PATCH_V6_SIZE) != 0 &&
        patches_from_old(5u, PATCH_V5_SIZE) != 0 &&
        patches_from_old(4u, PATCH_V4_SIZE) != 0 &&
        patches_from_old(3u, PATCH_V3_SIZE) != 0 &&
        patches_from_old(2u, PATCH_V2_SIZE) != 0)
        patches_from_old(1u, PATCH_V1_SIZE);
    bank.version = PATCH_VERSION;
}

static int patch_save(uint32_t slot, const patch_t *p)   /* 0 = saved (the old bank stays if it fails) */
{
    uint8_t was = bank.used[slot];
    patch_t old = bank.p[slot];
    int rc;
    bank.p[slot] = *p;
    bank.used[slot] = 1;
    if ((rc = store_save(&bank, sizeof bank)) != 0) {
        bank.p[slot] = old;
        bank.used[slot] = was;
    }
    return rc;
}

static int patch_get(uint32_t slot, patch_t *p)   /* 0 = there is a patch in the slot */
{
    if (slot >= PATCH_SLOTS || !bank.used[slot])
        return -1;
    *p = bank.p[slot];
    return 0;
}
