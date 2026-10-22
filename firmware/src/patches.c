/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's patches: 8 slots, kept in the platform's store (store_load / store_save: two flash sectors written in
 * turn, so a save cut short loses only itself). The bank is in RAM; a save writes it whole.
 * The format is checked by its size and PATCH_VERSION: a patch_t that changes shape never reads as wrong values.
 * New fields go at the end of patch_t; the older formats are converted when read (patch_from: the bank, a patch
 * file, the plugin's saved state), with the new fields as PATCH_INIT has them:
 *   1  up to fine2 (55 bytes)
 *   2  + the LFO (lfo_wave .. lfo_amt: 63 bytes)
 *   3  + the effects (cho_rate .. rev_shimmer: 81 bytes)
 *   4  + the mod envelope (menv_a .. menv_amt: 88 bytes)
 *   5  + velocity (vel_amp .. vel_mod: 91 bytes)
 *   6  + glide and the arpeggiator (glide .. arp_swing: 101 bytes)
 *   7  + the sequencer (seq_len .. seq_slide: 297 bytes)
 *   8  + the high-pass, the filter's type, the comb (hp_cut .. comb_spread: 301 bytes)
 *   9  + LFO 2 (lfo2_wave .. lfo2_amt: 306 bytes)
 *  10  - the pattern (seq_len .. seq_slide: the sequencer's patterns are seqs now, beside the bank); + mod envelope 2,
 *      the modulators' pitch and fine (menv2_a .. mfine). LFO 2's destinations past format 9's DEST_N move up 2
 *      (M1PITCH, M2PITCH before them); a modulator in MIX or RING takes its RATIO's pitch (up to +24 semitones)
 *  11  + the VCA's bias (vca: 0, closed, as before) */
#define PATCH_SLOTS 8u
#define PATCH_VERSION 11u
#define PATCH_V1_SIZE 55u                       /* format 1's patch_t: everything before lfo_wave */
#define PATCH_V2_SIZE 63u                       /* format 2's: everything before cho_rate */
#define PATCH_V3_SIZE 81u                       /* format 3's: everything before menv_a */
#define PATCH_V4_SIZE 88u                       /* format 4's: everything before vel_amp */
#define PATCH_V5_SIZE 91u                       /* format 5's: everything before glide */
#define PATCH_V6_SIZE 101u                      /* format 6's: everything before seq_len */
#define PATCH_V7_SIZE 297u                      /* format 7's: everything before hp_cut */
#define PATCH_V8_SIZE 301u                      /* format 8's: everything before lfo2_wave */
#define PATCH_V9_SIZE 306u                      /* format 9's: all of it */
#define PATCH_V10_SIZE 125u                     /* format 10's: everything before vca */
#define PATCH_OLD_MAX PATCH_V9_SIZE
#define PATCH_SEQ_AT PATCH_V6_SIZE              /* formats 7 .. 9: the pattern (seq_len, div, gate, pad, note[64], */
#define PATCH_SEQ_N (PATCH_V7_SIZE - PATCH_V6_SIZE)   /* trig[64], slide[64]) here, the rest after it */
_Static_assert(__builtin_offsetof(patch_t, lfo_wave) == PATCH_V1_SIZE, "format 1 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, cho_rate) == PATCH_V2_SIZE, "format 2 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, menv_a) == PATCH_V3_SIZE, "format 3 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, vel_amp) == PATCH_V4_SIZE, "format 4 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, glide) == PATCH_V5_SIZE, "format 5 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, hp_cut) == PATCH_V6_SIZE, "format 6 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, lfo2_wave) + PATCH_SEQ_N == PATCH_V8_SIZE, "format 8 is, but the pattern");
_Static_assert(__builtin_offsetof(patch_t, menv2_a) + PATCH_SEQ_N == PATCH_V9_SIZE, "format 9 is, but the pattern");
_Static_assert(__builtin_offsetof(patch_t, vca) == PATCH_V10_SIZE, "format 10 is a prefix of patch_t");

/* patch p from n bytes of format version (a patch file, the plugin's state, a slot): 0 = done */
static int patch_from(patch_t *p, const uint8_t *b, uint32_t n, uint32_t version)
{
    static const uint16_t SIZE[PATCH_VERSION] = {0, PATCH_V1_SIZE, PATCH_V2_SIZE, PATCH_V3_SIZE, PATCH_V4_SIZE,
                                                 PATCH_V5_SIZE, PATCH_V6_SIZE, PATCH_V7_SIZE, PATCH_V8_SIZE,
                                                 PATCH_V9_SIZE, PATCH_V10_SIZE};
    /* RATIO -> PITCH (0..48, 24 = in tune) and FINE: 0.5 .. 4 as near as they go, 5 and up as far: +24 */
    static const uint8_t R_SEMI[PR_RATIOS] = {12, 24, 31, 36, 43, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48};
    static const uint8_t R_FINE[PR_RATIOS] = {64, 64, 66, 64, 66, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};
    uint32_t o, k;
    if (version == PATCH_VERSION) {
        if (n != sizeof *p) return -1;
        memcpy(p, b, n);
        return 0;
    }
    if (version < 1u || version >= PATCH_VERSION || n != SIZE[version])
        return -1;
    *p = PATCH_INIT;                            /* the new fields: as the default sound has them */
    if (version >= 10u) {                       /* (format 10 on: each a prefix of the next) */
        memcpy(p, b, n);
        return 0;
    }
    memcpy(p, b, n < PATCH_SEQ_AT ? n : PATCH_SEQ_AT);
    if (n > PATCH_V7_SIZE)                      /* (after the pattern: the high-pass .. LFO 2) */
        memcpy((uint8_t *)p + PATCH_SEQ_AT, b + PATCH_V7_SIZE, n - PATCH_V7_SIZE);
    if (version == 9u && p->lfo2_dest >= DEST_V9_N)
        p->lfo2_dest = (uint8_t)(p->lfo2_dest + 2u);
    for (o = 0; o < NOSC; o++)
        for (k = 0; k < 2u; k++)
            if (p->osc[o].mode[k] != MODE_FM && p->osc[o].ratio[k + 1u] < PR_RATIOS) {
                p->mpitch[o][k] = R_SEMI[p->osc[o].ratio[k + 1u]];
                p->mfine[o][k] = R_FINE[p->osc[o].ratio[k + 1u]];
            }
    return 0;
}

static struct {
    uint32_t version;
    uint8_t used[PATCH_SLOTS];
    patch_t p[PATCH_SLOTS];
} bank;

/* the store holds the bank and, after it, the sequencer's patterns and song (engine.c: seqs). Older stores: format 9's
 * bank and the patterns (SEQS_VERSION 1), or a bank alone (formats 1 .. 9: then the patterns start empty, and the
 * sound that comes back at power-on brings its pattern in as pattern 1: seqs_from_slot, ui.c) */
#define SEQS_V1_SIZE 828u                       /* (seqs_t as SEQS_VERSION 1 had it: patterns of lane 0 alone) */
#define SEQS_V1_PAT 196u                        /* (its pattern_t: len, pad, note, trig, slide) */
_Static_assert(__builtin_offsetof(pattern_t, vel) == SEQS_V1_PAT, "a version 1 pattern is a prefix of pattern_t");
#define BANK_SIZE(psize) (4u + PATCH_SLOTS + PATCH_SLOTS * (psize))
#define STORE_NOW (sizeof bank + sizeof seqs)
#define STORE_OLD (BANK_SIZE(PATCH_V9_SIZE) + SEQS_V1_SIZE)   /* the biggest older store */
_Static_assert(BANK_SIZE(sizeof(patch_t)) == sizeof bank, "the bank has no padding");
static uint8_t store_buf[STORE_NOW > STORE_OLD ? STORE_NOW : STORE_OLD];
static uint8_t seqs_fresh;                      /* the patterns did not come from the store */
static uint32_t old_size;                       /* the bank was converted from patches of this size, with no patterns */

static int store_write(void)                    /* the bank and the patterns, together; 0 = saved */
{
    memcpy(store_buf, &bank, sizeof bank);
    memcpy(store_buf + sizeof bank, &seqs, sizeof seqs);
    return store_save(store_buf, STORE_NOW);
}

static void seqs_from_old(const uint8_t *q)     /* (formats 7 .. 9: a sound's single pattern, its bytes) -> pattern 1 */
{
    const uint8_t *note = q + 4, *trig = q + 68, *slide = q + 132;   /* (after seq_len, div, gate, pad) */
    uint32_t n = q[0] >= 1u && q[0] <= 64u ? q[0] : 16u, k;
    seqs.pat[0].len = (uint8_t)n;
    for (k = 0; k < 64u; k++) {
        seqs.pat[0].note[k] = note[k] & 127u;
        seqs.pat[0].trig[k] = trig[k] < SEQ_TIE ? trig[k] : SEQ_REST;
        seqs.pat[0].slide[k] = slide[k] ? 1u : 0u;
    }
    seqs.div = q[1] < DIV_N ? q[1] : DIV_16;
    seqs.gate = q[2] & 127u;
}

static int seqs_from(const uint8_t *q, uint32_t version)   /* the patterns as the store had them; 0 = done */
{
    uint32_t i, tail = SEQS_V1_SIZE - 4u - SEQ_PATS * SEQS_V1_PAT;   /* (version 1: after its patterns, the rest) */
    if (version == SEQS_VERSION) {
        memcpy(&seqs, q, sizeof seqs);
        return 0;
    }
    if (version != 1u)
        return -1;
    memset(&seqs, 0, sizeof seqs);              /* version 1: lane 0 alone; the chords' lanes empty */
    for (i = 0; i < SEQ_PATS; i++)
        memcpy(&seqs.pat[i], q + 4u + i * SEQS_V1_PAT, SEQS_V1_PAT);
    memcpy((uint8_t *)&seqs + __builtin_offsetof(seqs_t, div), q + 4u + SEQ_PATS * SEQS_V1_PAT, tail);
    seqs.version = SEQS_VERSION;
    return 0;
}
_Static_assert(sizeof(seqs_t) - __builtin_offsetof(seqs_t, div) == SEQS_V1_SIZE - 4u - SEQ_PATS * SEQS_V1_PAT,
               "after the patterns, seqs_t is as version 1 had it");

/* the store as the bank of format version (patches psize bytes), then patterns of sversion (ssize bytes; 0: none) */
static int store_try(uint32_t version, uint32_t psize, uint32_t sversion, uint32_t ssize)
{
    uint32_t n = BANK_SIZE(psize), v, s;
    if (store_load(store_buf, n + ssize) != 0)
        return -1;
    memcpy(&v, store_buf, 4);
    if (v != version)
        return -1;
    if (ssize) {
        memcpy(&v, store_buf + n, 4);
        if (v != sversion || seqs_from(store_buf + n, sversion) != 0)
            return -1;
    } else {
        seqs_init();
        seqs_fresh = 1;
    }
    for (s = 0; s < PATCH_SLOTS; s++) {
        if (patch_from(&bank.p[s], store_buf + 4u + PATCH_SLOTS + s * psize, psize, version) != 0)
            bank.p[s] = PATCH_INIT;
        bank.used[s] = store_buf[4u + s];
    }
    bank.version = PATCH_VERSION;
    old_size = ssize ? 0u : psize;              /* (seqs_from_slot: a pattern in a patch, still in store_buf) */
    return 0;
}

static void seqs_from_slot(uint32_t s)         /* the converted bank's slot s had a pattern: it is pattern 1 */
{
    if (old_size >= PATCH_V7_SIZE && s < PATCH_SLOTS)
        seqs_from_old(store_buf + BANK_SIZE(0) + s * old_size + PATCH_SEQ_AT);
}

static void patches_load(void)                  /* at boot: the saved bank (converted if older), or an empty one */
{
    static const uint16_t OLD[] = {PATCH_V9_SIZE, PATCH_V8_SIZE, PATCH_V7_SIZE, PATCH_V6_SIZE, PATCH_V5_SIZE,
                                   PATCH_V4_SIZE, PATCH_V3_SIZE, PATCH_V2_SIZE, PATCH_V1_SIZE};   /* formats 9 .. 1 */
    uint32_t i;
    seqs_fresh = 0;
    old_size = 0;
    if (store_try(PATCH_VERSION, sizeof(patch_t), SEQS_VERSION, sizeof seqs) == 0 ||
        store_try(10u, PATCH_V10_SIZE, SEQS_VERSION, sizeof seqs) == 0 ||   /* (0.9) */
        store_try(10u, PATCH_V10_SIZE, 1u, SEQS_V1_SIZE) == 0 ||
        store_try(9u, PATCH_V9_SIZE, 1u, SEQS_V1_SIZE) == 0)   /* (kept in RAM; the next save writes them anew) */
        return;
    for (i = 0; i < sizeof OLD / sizeof OLD[0]; i++)
        if (store_try(9u - i, OLD[i], 0, 0) == 0)
            return;
    memset(&bank, 0, sizeof bank);              /* none: an empty bank, empty patterns */
    bank.version = PATCH_VERSION;
    seqs_init();
    seqs_fresh = 1;
}

static int patch_save(uint32_t slot, const patch_t *p)   /* 0 = saved (the old bank stays if it fails) */
{
    uint8_t was = bank.used[slot];
    patch_t old = bank.p[slot];
    int rc;
    bank.p[slot] = *p;
    bank.used[slot] = 1;
    if ((rc = store_write()) != 0) {
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
