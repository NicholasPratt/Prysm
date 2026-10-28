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
 *  11  + the VCA's bias (vca: 0, closed, as before); LFO 2's destinations past M2PITCH move up 1 (LFOSHAPE)
 *  12  + the noise and the routing (noise_type .. route: no noise, everything through the filter, as before)
 *  13  + the keyboard split (split, split_oct: off, as before)
 *  14  + the mod matrix, MOD FX, CRUSH (mm_src .. crush_rate: all off, as before)
 *  15  + the high-pass's resonance (hp_res: none, as before) */
#define PATCH_SLOTS 8u
#define PATCH_VERSION 15u
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
#define PATCH_V11_SIZE 126u                     /* format 11's: everything before noise_type */
#define PATCH_V12_SIZE 132u                     /* format 12's: everything before split */
#define PATCH_V13_SIZE 135u                     /* format 13's: everything before mm_src */
#define PATCH_V14_SIZE 155u                     /* format 14's: everything before hp_res */
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
_Static_assert(__builtin_offsetof(patch_t, noise_type) == PATCH_V11_SIZE, "format 11 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, split) == PATCH_V12_SIZE, "format 12 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, mm_src) == PATCH_V13_SIZE, "format 13 is a prefix of patch_t");
_Static_assert(__builtin_offsetof(patch_t, hp_res) == PATCH_V14_SIZE, "format 14 is a prefix of patch_t");

/* patch p from n bytes of format version (a patch file, the plugin's state, a slot): 0 = done */
static int patch_from(patch_t *p, const uint8_t *b, uint32_t n, uint32_t version)
{
    static const uint16_t SIZE[PATCH_VERSION] = {0, PATCH_V1_SIZE, PATCH_V2_SIZE, PATCH_V3_SIZE, PATCH_V4_SIZE,
                                                 PATCH_V5_SIZE, PATCH_V6_SIZE, PATCH_V7_SIZE, PATCH_V8_SIZE,
                                                 PATCH_V9_SIZE, PATCH_V10_SIZE, PATCH_V11_SIZE, PATCH_V12_SIZE,
                                                 PATCH_V13_SIZE, PATCH_V14_SIZE};
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
        if (version == 10u && p->lfo2_dest >= DEST_V10_N)
            p->lfo2_dest = (uint8_t)(p->lfo2_dest + 1u);
        return 0;
    }
    memcpy(p, b, n < PATCH_SEQ_AT ? n : PATCH_SEQ_AT);
    if (n > PATCH_V7_SIZE)                      /* (after the pattern: the high-pass .. LFO 2) */
        memcpy((uint8_t *)p + PATCH_SEQ_AT, b + PATCH_V7_SIZE, n - PATCH_V7_SIZE);
    if (version == 9u && p->lfo2_dest >= DEST_V9_N)
        p->lfo2_dest = (uint8_t)(p->lfo2_dest + 3u);
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

/* the store holds the bank; the store's own sequencer (engine.c: seqs: the patterns, the song, the drum kit) is kept
 * apart (seqstore SEQ_OWN). Until format 14 it was in the store, after the bank: format 9's bank and the patterns
 * (SEQS_VERSION 1) .. format 13's and SEQS_VERSION 4; or a bank alone (formats 1 .. 9: then the patterns start empty,
 * and the sound that comes back at power-on brings its pattern in as pattern 1: seqs_from_slot, ui.c). SEQS_VERSION 2
 * had no drums (their lanes start empty, the kit as seqs_init has it); 3 no ratchets; 4 three drums and no locks */
#define SEQS_V1_SIZE 828u                       /* (seqs_t as SEQS_VERSION 1 had it: patterns of lane 0 alone) */
#define SEQS_V1_PAT 196u                        /* (its pattern_t: len, pad, note, trig, slide) */
#define SEQS_V2_SIZE 2620u                      /* (version 2's: patterns with chords, no drums) */
#define SEQS_V2_PAT 644u
#define SEQS_TAIL 40u                           /* (after the patterns: div .. song_rep, as every version has them) */
#define SEQS_V3_SIZE 2828u                      /* (version 3's: no ratchets) */
#define SEQS_V3_PAT 692u
#define SEQS_V4_SIZE 2960u                      /* (version 4's: three drums) */
#define SEQS_V4_PAT 724u
#define SEQS_V4_DRUMS 3u
_Static_assert(__builtin_offsetof(pattern_t, vel) == SEQS_V1_PAT, "a version 1 pattern is a prefix of pattern_t");
_Static_assert(__builtin_offsetof(pattern_t, dhit) == SEQS_V2_PAT, "a version 2 pattern is a prefix of pattern_t");
_Static_assert(__builtin_offsetof(seqs_t, dr_lvl) - __builtin_offsetof(seqs_t, div) == SEQS_TAIL,
               "after the patterns, seqs_t starts as the old versions had it");
_Static_assert(4u + SEQ_PATS * SEQS_V1_PAT + SEQS_TAIL == SEQS_V1_SIZE && 4u + SEQ_PATS * SEQS_V2_PAT + SEQS_TAIL == SEQS_V2_SIZE,
               "the old versions' sizes");
_Static_assert(SEQS_V2_PAT + 2u * SEQS_V4_DRUMS * 8u == SEQS_V3_PAT && SEQS_V3_PAT + 32u == SEQS_V4_PAT &&
               4u + SEQ_PATS * SEQS_V3_PAT + SEQS_TAIL + 4u * SEQS_V4_DRUMS + 4u == SEQS_V3_SIZE &&
               4u + SEQ_PATS * SEQS_V4_PAT + SEQS_TAIL + 4u * SEQS_V4_DRUMS + 5u + 3u == SEQS_V4_SIZE,
               "versions 3 and 4: the drums' lanes, the ratchets, the kit (and its padding)");
#define BANK_SIZE(psize) (4u + PATCH_SLOTS + PATCH_SLOTS * (psize))
#define STORE_OLD (BANK_SIZE(PATCH_V13_SIZE) + SEQS_V4_SIZE)   /* the biggest older store (1.3's) */
#define SEQ_OWN PATCH_SLOTS                     /* (the store's own sequencer: the seqstore after the slots') */
_Static_assert(BANK_SIZE(sizeof(patch_t)) == sizeof bank, "the bank has no padding");
_Static_assert(STORE_OLD >= sizeof bank && STORE_OLD >= sizeof(seqs_t), "store_buf holds an older store, the bank, a sequencer");
static uint8_t store_buf[STORE_OLD];
static uint8_t seqs_fresh;                      /* the patterns did not come from the store */
static uint8_t seqs_moved;                      /* the store's own sequencer came from an older store, after its bank */
static uint32_t old_size;                       /* the bank was converted from patches of this size, with no patterns */

/* Each slot has a sequencer of its own, kept apart from the bank (seqstore_load / seqstore_save, platform.c). seqs is
 * the one playing; seq_owner says whose: a slot, or -1, the store's own (the one sequencer there was before 1.3, and
 * the one playing until a slot's is loaded) */
_Static_assert(PATCH_SLOTS + 1u == SEQSTORE_N, "a sequencer for each slot, and the store's own");
static int32_t seq_owner = -1;

static int store_write(void)                    /* the bank (and the store's own sequencer, if it plays); 0 = saved */
{
    int rc;
    memcpy(store_buf, &bank, sizeof bank);
    rc = store_save(store_buf, sizeof bank);
    return rc == 0 && seq_owner < 0 ? seqstore_save(SEQ_OWN, &seqs, sizeof seqs) : rc;
}
static void seqs_keep(void)                     /* (boot: seqs is the store's own; from an older store: kept apart now) */
{
    if (seqs_moved && seqstore_save(SEQ_OWN, &seqs, sizeof seqs) == 0)
        seqs_moved = 0;
}
static int seqs_write(void)                     /* the sequencer playing, to its owner; 0 = saved */
{
    return seqstore_save(seq_owner >= 0 ? (uint32_t)seq_owner : SEQ_OWN, &seqs, sizeof seqs);
}

static uint32_t seqs_size(uint32_t version)     /* a sequencer of that version: its bytes (0: no such version) */
{
    static const uint16_t SIZE[SEQS_VERSION] = {0, SEQS_V1_SIZE, SEQS_V2_SIZE, SEQS_V3_SIZE, SEQS_V4_SIZE};
    return version == SEQS_VERSION ? (uint32_t)sizeof(seqs_t) : version < SEQS_VERSION ? SIZE[version] : 0u;
}

/* s from q, a sequencer's bytes as version had them (q apart from s): the patterns, the song, the kit; what the
 * version had not, as seqs_blank has it (version 1: the chords' lanes empty; 2: no drums; 3: no ratchets; 4: no
 * clap, no locks). 0 = done */
static int seqs_conv(seqs_t *s, const uint8_t *q, uint32_t version)
{
    static const uint16_t PAT[SEQS_VERSION] = {0, SEQS_V1_PAT, SEQS_V2_PAT, SEQS_V3_PAT, SEQS_V4_PAT};
    uint32_t i, d, ps;
    const uint8_t *t;
    if (version == SEQS_VERSION) {
        memcpy(s, q, sizeof *s);
        return 0;
    }
    if (version < 1u || version > SEQS_VERSION)
        return -1;
    seqs_blank(s);
    ps = PAT[version];
    for (i = 0; i < SEQ_PATS; i++) {
        const uint8_t *o = q + 4u + i * ps;
        memcpy(&s->pat[i], o, ps < SEQS_V2_PAT ? ps : SEQS_V2_PAT);
        if (ps < SEQS_V3_PAT)
            continue;
        for (d = 0; d < SEQS_V4_DRUMS; d++) {   /* (the drums' lanes: kick, snare, hat; then their accents) */
            memcpy(s->pat[i].dhit[d], o + SEQS_V2_PAT + d * 8u, 8u);
            memcpy(s->pat[i].dacc[d], o + SEQS_V2_PAT + (SEQS_V4_DRUMS + d) * 8u, 8u);
        }
        if (ps >= SEQS_V4_PAT)
            memcpy(s->pat[i].dret, o + SEQS_V3_PAT, sizeof s->pat[i].dret);
    }
    t = q + 4u + SEQ_PATS * ps;
    memcpy((uint8_t *)s + __builtin_offsetof(seqs_t, div), t, SEQS_TAIL);
    if (version >= 3u) {                        /* the kit: lvl, tune, dec of 3; drive, crush, dly, rev; flt of 3; ramp */
        t += SEQS_TAIL;
        memcpy(s->dr_lvl, t, SEQS_V4_DRUMS);
        memcpy(s->dr_tune, t + 3u, SEQS_V4_DRUMS);
        memcpy(s->dr_dec, t + 6u, SEQS_V4_DRUMS);
        s->dr_drive = t[9]; s->dr_crush = t[10]; s->dr_dly = t[11]; s->dr_rev = t[12];
        memcpy(s->dr_flt, t + 13u, SEQS_V4_DRUMS);
        if (version >= 4u) s->dr_ramp = t[16];
    }
    s->version = SEQS_VERSION;
    return 0;
}
static int seqs_from(const uint8_t *q, uint32_t version)   /* seqs, from a sequencer as version had it; 0 = done */
{
    return seqs_conv(&seqs, q, version);
}
/* seqstore i's sequencer (of any version) into s; 0 = it has one. (Older ones: converted; the next save writes them
 * anew.) store_buf holds the bytes on the way */
static int seqs_load(uint32_t i, seqs_t *s)
{
    uint32_t v, had;
    if (seqstore_load(i, s, sizeof *s) == 0 && s->version == SEQS_VERSION)
        return 0;
    for (v = SEQS_VERSION - 1u; v >= 3u; v--)   /* (as 1.3 saved them) */
        if (seqstore_load(i, store_buf, seqs_size(v)) == 0 && (memcpy(&had, store_buf, 4), had == v))
            return seqs_conv(s, store_buf, v);
    return -1;
}
static int seqs_fetch(uint32_t slot)            /* slot's own sequencer into seqs_next (engine.c); 0 = it has one */
{
    return seqs_load(slot, &seqs_next);
}
static void seqs_migrate(void)                  /* (boot) no slot has a sequencer yet: each used one gets the one there is */
{
    uint32_t s;
    for (s = 0; s < PATCH_SLOTS; s++)
        if (seqs_fetch(s) == 0)
            return;                             /* (done before) */
    for (s = 0; s < PATCH_SLOTS; s++)
        if (bank.used[s])
            seqstore_save(s, &seqs, sizeof seqs);
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

/* the store as the bank of format version (patches psize bytes), then patterns of sversion (ssize bytes; 0: none;
 * -1: kept apart, as from format 14) */
static int store_try(uint32_t version, uint32_t psize, uint32_t sversion, uint32_t ssize)
{
    uint32_t n = BANK_SIZE(psize), v, s, apart = ssize == ~0u;
    if (store_load(store_buf, n + (apart ? 0u : ssize)) != 0)
        return -1;
    memcpy(&v, store_buf, 4);
    if (v != version)
        return -1;
    if (apart) {
        if (seqs_load(SEQ_OWN, &seqs) != 0)
            seqs_init();                        /* (none saved yet) */
    } else if (ssize) {
        memcpy(&v, store_buf + n, 4);
        if (v != sversion || seqs_from(store_buf + n, sversion) != 0)
            return -1;
        seqs_moved = 1;
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
    seqs_moved = 0;
    if (store_try(PATCH_VERSION, sizeof(patch_t), SEQS_VERSION, ~0u) == 0 ||
        store_try(14u, PATCH_V14_SIZE, SEQS_VERSION, ~0u) == 0 ||   /* (1.4's dev builds) */
        store_try(13u, PATCH_V13_SIZE, 4u, SEQS_V4_SIZE) == 0 ||   /* (1.3 and the audit's) */
        store_try(13u, PATCH_V13_SIZE, 3u, SEQS_V3_SIZE) == 0 ||   /* (1.3) */
        store_try(12u, PATCH_V12_SIZE, 3u, SEQS_V3_SIZE) == 0 ||   /* (1.2) */
        store_try(11u, PATCH_V11_SIZE, 2u, SEQS_V2_SIZE) == 0 ||   /* (1.0, 1.1) */
        store_try(10u, PATCH_V10_SIZE, 2u, SEQS_V2_SIZE) == 0 ||   /* (0.9) */
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
