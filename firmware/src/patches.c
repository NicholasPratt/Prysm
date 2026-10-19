/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patches: 8 slots, kept in the platform's store (store_load / store_save: two flash sectors written in
 * turn, so a save cut short loses only itself). The bank is in RAM; a save writes it whole.
 * The format is checked by its size and PATCH_VERSION: a patch_t that changes shape reads as an empty bank, never
 * as wrong values (bump PATCH_VERSION, or convert the old bank, when patch_t changes). */
#define PATCH_SLOTS 8u
#define PATCH_VERSION 1u

static struct {
    uint32_t version;
    uint8_t used[PATCH_SLOTS];
    patch_t p[PATCH_SLOTS];
} bank;

static void patches_load(void)                  /* at boot: the saved bank, or an empty one */
{
    if (store_load(&bank, sizeof bank) != 0 || bank.version != PATCH_VERSION) {
        memset(&bank, 0, sizeof bank);
        bank.version = PATCH_VERSION;
    }
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
