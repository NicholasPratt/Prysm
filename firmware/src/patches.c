/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patches: 8 slots, kept in the platform's store (store_load / store_save: two flash sectors written in
 * turn, so a save cut short loses only itself). The bank is in RAM; a save writes it whole.
 * The format is checked by its size and PATCH_VERSION: a patch_t that changes shape never reads as wrong values.
 * New fields go at the end of patch_t; the older formats are converted when read (patches_load), with the new
 * fields as PATCH_INIT has them:
 *   1  up to fine2 (55 bytes)
 *   2  + the LFO (lfo_wave .. lfo_amt) */
#define PATCH_SLOTS 8u
#define PATCH_VERSION 2u
#define PATCH_V1_SIZE 55u                       /* format 1's patch_t: everything before lfo_wave */
_Static_assert(__builtin_offsetof(patch_t, lfo_wave) == PATCH_V1_SIZE, "format 1 is a prefix of patch_t");

static struct {
    uint32_t version;
    uint8_t used[PATCH_SLOTS];
    patch_t p[PATCH_SLOTS];
} bank;

static int patches_from_v1(void)                /* a format 1 bank -> bank; 0 = there was one */
{
    static struct {
        uint32_t version;
        uint8_t used[PATCH_SLOTS];
        uint8_t p[PATCH_SLOTS][PATCH_V1_SIZE];
    } old;
    uint32_t s;
    if (store_load(&old, sizeof old) != 0 || old.version != 1u)
        return -1;
    for (s = 0; s < PATCH_SLOTS; s++) {
        bank.p[s] = PATCH_INIT;                 /* the new fields: as the default sound has them */
        memcpy(&bank.p[s], old.p[s], PATCH_V1_SIZE);
        bank.used[s] = old.used[s];
    }
    return 0;
}

static void patches_load(void)                  /* at boot: the saved bank (converted if older), or an empty one */
{
    if (store_load(&bank, sizeof bank) == 0 && bank.version == PATCH_VERSION)
        return;
    memset(&bank, 0, sizeof bank);
    patches_from_v1();                          /* (kept in RAM; written in the new format by the next save) */
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
