/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patch bank on a computer: every older format converts to the current one, keeping every sound and
 * giving what is new the default's values; the patterns come along (format 9 with the patterns, or a sound's own
 * pattern in formats 7 .. 9); the format saves and loads back.
 *   tests/run_engine.sh runs it */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/engine.c"

#define STORE_MAX (0x1000u - 16u)                  /* (as platform.c) */
static uint8_t store[4096];                     /* a fake store (platform.c's store_load / store_save) */
static uint32_t store_n;
static int store_load(void *p, uint32_t n) { if (n != store_n) return -1; memcpy(p, store, n); return 0; }
static int store_save(const void *p, uint32_t n) { memcpy(store, p, n); store_n = n; return 0; }
#include "../firmware/src/patches.c"

static int fails;
#define CHECK(c, ...) do { printf("  %s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)

/* sound c as a format of `size` bytes had it: formats 7 .. 9 with a pattern (seq: its 196 bytes) in the middle */
static void as_old(uint8_t *d, const patch_t *c, uint32_t size, const uint8_t *seq)
{
    memcpy(d, c, size < PATCH_SEQ_AT ? size : PATCH_SEQ_AT);
    if (size > PATCH_SEQ_AT) {
        memcpy(d + PATCH_SEQ_AT, seq, PATCH_SEQ_N);
        memcpy(d + PATCH_V7_SIZE, (const uint8_t *)c + PATCH_SEQ_AT, size - PATCH_V7_SIZE);
    }
}
static int same_old(const patch_t *a, const patch_t *c, uint32_t size)   /* the fields a format of size has: equal */
{
    if (memcmp(a, c, size < PATCH_SEQ_AT ? size : PATCH_SEQ_AT)) return 0;
    return size <= PATCH_V7_SIZE ||
           !memcmp((const uint8_t *)a + PATCH_SEQ_AT, (const uint8_t *)c + PATCH_SEQ_AT, size - PATCH_V7_SIZE);
}
static uint8_t old_store[4096];
static void seqs_as_v1(uint8_t *d)              /* seqs as SEQS_VERSION 1 had them: lane 0 alone */
{
    uint32_t i, one = 1;
    memcpy(d, &one, 4);
    for (i = 0; i < SEQ_PATS; i++)
        memcpy(d + 4u + i * SEQS_V1_PAT, &seqs.pat[i], SEQS_V1_PAT);
    memcpy(d + 4u + SEQ_PATS * SEQS_V1_PAT, (uint8_t *)&seqs + __builtin_offsetof(seqs_t, div),
           sizeof seqs - __builtin_offsetof(seqs_t, div));
}
static void store_old(uint32_t version, uint32_t size, uint32_t slot, const patch_t *c, const uint8_t *seq)
{                                               /* a bank of that format, with c in slot (and nothing after it) */
    memset(old_store, 0, sizeof old_store);
    memcpy(old_store, &version, 4);
    old_store[4u + slot] = 1;
    as_old(old_store + BANK_SIZE(0) + slot * size, c, size, seq);
    store_save(old_store, BANK_SIZE(size));
    memset(&bank, 0, sizeof bank);
    patches_load();
}

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
    CHECK(seqs_fresh, "a bank from before the patterns: the patterns start empty (to take the sound's pattern)");
    CHECK(patch_save(4, &got) == 0 && store_n == sizeof bank + sizeof seqs, "a save writes format %u and the patterns (%u bytes)",
          (unsigned)PATCH_VERSION, (unsigned)store_n);
    seqs.pat[2].len = 33; seqs.pat[2].trig[5] = SEQ_TIE; seqs.song_len = 7; seqs.mode = 1;
    CHECK(store_write() == 0, "the patterns saved");
    memset(&bank, 0, sizeof bank);
    seqs_init();
    patches_load();
    CHECK(patch_get(4, &a) == 0 && !memcmp(&a, &got, sizeof a), "the new format loads back as it was saved");
    CHECK(!seqs_fresh && seqs.pat[2].len == 33 && seqs.pat[2].trig[5] == SEQ_TIE && seqs.song_len == 7 && seqs.mode == 1,
          "the patterns and the song load back as they were saved");
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
    {                                           /* format 5 (with velocity, before glide and the arp) -> 6 */
        struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V5_SIZE]; } v5;
        patch_t c = PATCH_INIT;
        c.vel_cut = 99; c.osc[0].pos[1] = 33;
        memset(&v5, 0, sizeof v5);
        v5.version = 5;
        v5.used[6] = 1; memcpy(v5.p[6], &c, PATCH_V5_SIZE);
        store_save(&v5, sizeof v5);
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(6, &a) == 0 && !memcmp(&a, &c, PATCH_V5_SIZE) && bank.version == PATCH_VERSION,
              "format 5: slot 7's sound and velocity kept");
        CHECK(a.arp_on == 0 && a.glide == 0, "format 5: the arp comes in off, no glide");
    }
    {                                           /* format 6 (with the arp, before the sequencer) -> 7 */
        struct { uint32_t version; uint8_t used[PATCH_SLOTS]; uint8_t p[PATCH_SLOTS][PATCH_V6_SIZE]; } v6;
        patch_t c = PATCH_INIT;
        c.arp_on = 1; c.arp_mode = ARP_UPDN; c.glide = 40;
        memset(&v6, 0, sizeof v6);
        v6.version = 6;
        v6.used[1] = 1; memcpy(v6.p[1], &c, PATCH_V6_SIZE);
        store_save(&v6, sizeof v6);
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(1, &a) == 0 && !memcmp(&a, &c, PATCH_V6_SIZE) && bank.version == PATCH_VERSION,
              "format 6: slot 2's sound and arp kept");
        CHECK(seqs_fresh && seqs.pat[0].len == 16 && seqs.pat[0].trig[0] == SEQ_REST,
              "format 6: the patterns come in empty, 16 steps");
    }
    {                                           /* formats 7, 8, 9 alone: each sound had a pattern; slot's -> P1 */
        static const uint16_t SIZE[3] = {PATCH_V7_SIZE, PATCH_V8_SIZE, PATCH_V9_SIZE};
        uint8_t seq[PATCH_SEQ_N];
        uint32_t v;
        patch_t c = PATCH_INIT;
        c.cut = 5; c.flt_type = FLT_COMBM; c.comb_spread = 77; c.hp_cut = 9; c.lfo2_amt = 99; c.lfo2_dest = DEST_CUTOFF;
        memset(seq, 0, sizeof seq);
        seq[0] = 12; seq[1] = DIV_8; seq[2] = 90;   /* len, div, gate, pad; note[64], trig[64], slide[64] */
        seq[4 + 3] = 60; seq[68 + 3] = SEQ_ACCENT; seq[132 + 3] = 1;
        for (v = 7; v <= 9; v++) {
            store_old(v, SIZE[v - 7u], 4, &c, seq);
            CHECK(patch_get(4, &a) == 0 && same_old(&a, &c, SIZE[v - 7u]) && bank.version == PATCH_VERSION,
                  "format %u: slot 5's sound kept", (unsigned)v);
            seqs_from_slot(4);
            CHECK(seqs_fresh && seqs.pat[0].len == 12 && seqs.pat[0].note[3] == 60 && seqs.pat[0].trig[3] == SEQ_ACCENT &&
                  seqs.pat[0].slide[3] == 1 && seqs.div == DIV_8 && seqs.gate == 90 && seqs.pat[1].len == 16,
                  "format %u: its pattern becomes pattern 1", (unsigned)v);
        }
        CHECK(a.menv2_amt == 64 && a.mpitch[1][1] == 24 && a.mfine[0][0] == 64,
              "format 9: mod envelope 2 with no amount, the modulators in tune");
    }
    {                                           /* format 9 with the patterns beside it (SEQS_VERSION 1) */
        uint8_t seq[PATCH_SEQ_N] = {16};
        patch_t c = PATCH_INIT;
        c.lfo2_dest = DEST_V9_N;                /* (format 9's RES) */
        c.osc[1].mode[0] = MODE_MIX; c.osc[1].ratio[1] = 3;   /* x2: +12 */
        c.osc[0].mode[1] = MODE_RING; c.osc[0].ratio[2] = 2;  /* x1.5: +7, +2 cents */
        seqs_init();
        seqs.pat[3].len = 21; seqs.pat[3].note[5] = 62; seqs.pat[3].trig[5] = SEQ_ACCENT; seqs.song_len = 3;
        seqs.song_rep[2] = 5; seqs.div = DIV_8T;
        memset(old_store, 0, sizeof old_store);
        old_store[0] = 9; old_store[4u + 1u] = 1;
        as_old(old_store + BANK_SIZE(0) + PATCH_V9_SIZE, &c, PATCH_V9_SIZE, seq);
        seqs_as_v1(old_store + BANK_SIZE(PATCH_V9_SIZE));
        store_save(old_store, BANK_SIZE(PATCH_V9_SIZE) + SEQS_V1_SIZE);
        seqs_init();
        memset(&bank, 0, sizeof bank);
        patches_load();
        CHECK(patch_get(1, &a) == 0 && !seqs_fresh && seqs.pat[3].len == 21 && seqs.pat[3].note[5] == 62 &&
              seqs.pat[3].trig[5] == SEQ_ACCENT && seqs.song_len == 3 && seqs.song_rep[2] == 5 && seqs.div == DIV_8T &&
              seqs.version == SEQS_VERSION, "format 9 and the patterns (version 1): the sound and the patterns kept");
        CHECK(seqs.pat[3].vel[5] == 0 && seqs.pat[3].xvel[5][2] == 0 && seqs.pat[3].xnote[63][0] == 0,
              "version 1 patterns: the chords' lanes come in empty");
        CHECK(a.lfo2_dest == DEST_RES, "format 9: LFO 2 on RES stays on RES (%u)", (unsigned)a.lfo2_dest);
        CHECK(a.mpitch[1][0] == 36 && a.mfine[1][0] == 64 && a.mpitch[0][1] == 31 && a.mfine[0][1] == 66 &&
              a.mpitch[0][0] == 24, "MIX / RING: the ratio becomes the pitch (x2: +12, x1.5: +7 and 2 cents); FM: in tune");
        {                                       /* a patch file of format 9 (the editor, the plugin) */
            uint8_t f[PATCH_V9_SIZE];
            as_old(f, &c, PATCH_V9_SIZE, seq);
            CHECK(patch_from(&got, f, sizeof f, 9) == 0 && !memcmp(&got, &a, sizeof got), "a format 9 file converts the same");
            CHECK(patch_from(&got, f, sizeof f - 1u, 9) != 0 && patch_from(&got, f, sizeof f, 11) != 0,
                  "a file of the wrong size or a newer format: refused");
        }
    }
    {                                           /* the plugin's saved patterns, version 1 (prysm_seqs_load) */
        uint8_t v1[SEQS_V1_SIZE];
        seqs_init();
        seqs.pat[1].note[9] = 70; seqs.pat[1].trig[9] = SEQ_NOTE; seqs.mode = 1;
        seqs_as_v1(v1);
        seqs_init();
        CHECK(seqs_from(v1, 1) == 0 && seqs.pat[1].note[9] == 70 && seqs.pat[1].trig[9] == SEQ_NOTE && seqs.mode == 1,
              "version 1 patterns convert (the plugin's state)");
        CHECK(seqs_from(v1, 3) != 0, "a newer version: refused");
    }
    CHECK(sizeof bank + sizeof seqs <= STORE_MAX, "the bank and the patterns fit the store (%u of %u bytes)",
          (unsigned)(sizeof bank + sizeof seqs), (unsigned)STORE_MAX);
    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
