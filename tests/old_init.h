/* SPDX-License-Identifier: GPL-3.0-only */
/* The default sound as it was before 1.8 (OSC 2 at 70 on the MIX page, OSC 1's mod 1 at 70, the filter envelope at
 * 88, both parts), for the tests that use it as a sound to play: OSC 2 heard (DUAL, SPLIT), the filter moving.
 * Include after the firmware's sources; PATCH_INIT from here on is that sound. */
static const patch_t *old_init(void)
{
    static patch_t p;
    uint32_t i;
    p = PATCH_INIT;
    p.osc[0].amt[0] = 70; p.lvl2 = 70; p.fenv = 88;
    for (i = 0; i < PART_N; i++)
        if (PART_OFF[i] == __builtin_offsetof(patch_t, fenv)) p.pb[i] = 88;
    return &p;
}
#define PATCH_INIT (*old_init())
