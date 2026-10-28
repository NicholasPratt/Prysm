/* SPDX-License-Identifier: GPL-3.0-only */
/* The hat's ratchets on a computer, through the app's own code and the engine: a step of 8 hits spaced evenly over it,
 * RAMP fading and swelling them, a render holding two of them sounding as renders of a block each, the ROLL keys,
 * a hit taken away taking its ratchet, and a sequencer saved by 1.3 (version 3) read with none.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static void setup(uint32_t ramp)                /* 240 BPM, 1/32 steps (1378 frames), the hat on step 1, 8 hits */
{
    static int32_t blk[2u * BLK];
    engine_seq_play(0);                         /* (stopped a block: the next PLAY from the top) */
    engine_render(blk, BLK);
    engine_init();
    edit = PATCH_INIT;
    edit.cho_mix = edit.dly_mix = edit.rev_mix = 0;
    edit.arp_bpm = 200;
    seqs_init();
    seqs.div = DIV_32;
    seqs.dr_ramp = (uint8_t)ramp;
    drum_put(&seqs.pat[0], 2u, 0, 1u);
    ratchet_put(&seqs.pat[0], 0, 5u);           /* (RATCHET[5]: 8) */
    engine_set(&edit);
    engine_sync(0);
    engine_seq_play(1);
}

static uint32_t hit_at[16], hit_amp[16], hits;
static void run_blocks(uint32_t frames)         /* renders of a block each, the hat's hits noted (when, how loud) */
{
    static int32_t blk[2u * BLK];
    uint32_t t, prev = 0;
    const int8_t *was = 0;
    hits = 0;
    for (t = 0; t < frames; t += BLK) {
        memset(blk, 0, sizeof blk);
        engine_render(blk, BLK);
        if (drv[2].d && (!was || drv[2].pos_q16 < prev) && hits < 16u) {
            hit_at[hits] = t;
            hit_amp[hits++] = (uint32_t)drv[2].amp;
        }
        was = drv[2].d;
        prev = drv[2].pos_q16;
    }
}

int main(void)
{
    static int32_t a[2u * 8192u], b[2u * 8192u];
    uint32_t i, ok, step;
    printf("ratchet checks\n");

    setup(64);
    step = eg.seq_len_q8 >> 8;
    run_blocks(step + step / 2u);
    ok = hits == 8u;
    for (i = 1; ok && i < hits; i++) {
        int32_t gap = (int32_t)(hit_at[i] - hit_at[i - 1]) - (int32_t)(step / 8u);
        ok = gap > -(int32_t)BLK && gap < (int32_t)BLK;
    }
    CHECK(ok, "ROLL 8 on a 1/32 step (%u frames): %u hits, %u frames apart", step, hits, step / 8u);
    for (i = 1, ok = 1; i < hits; i++) ok &= hit_amp[i] == hit_amp[0];
    CHECK(ok, "RAMP EVEN: all as loud");

    setup(0);
    run_blocks(step);
    for (i = 1, ok = hits == 8u; i < hits; i++) ok &= hit_amp[i] < hit_amp[i - 1];
    CHECK(ok, "RAMP FADE 64: each softer than the last");
    setup(127);
    run_blocks(step);
    for (i = 1, ok = hits == 8u; i < hits; i++) ok &= hit_amp[i] > hit_amp[i - 1];
    CHECK(ok, "RAMP SWELL 63: each louder, into the step's level");

    setup(64);                                  /* the same, in renders of a block, then of 512 (two hits each) */
    for (i = 0; i < 8192u; i += BLK) { memset(a + 2u * i, 0, 2u * BLK * sizeof a[0]); engine_render(a + 2u * i, BLK); }
    setup(64);
    for (i = 0; i < 8192u; i += 512u) { memset(b + 2u * i, 0, 1024u * sizeof b[0]); engine_render(b + 2u * i, 512u); }
    for (i = 0, ok = 1; i < 2u * 8192u; i++) ok &= a[i] == b[i];
    CHECK(ok, "renders of 512 frames (two hits in one) sound as renders of a block each");

    seqs_init();                                /* the keys on the ROLL row: OFF 2 3 4 6 8 in turn, a hit with it */
    page = PG_DRUM;
    row[PG_DRUM] = DR_ROLL;
    dr_cursor = 0;
    ui_drum_key(2);                             /* (G3: step 2) */
    CHECK(drum_at(seq_ed(), 2u, 1u) == 1u && RATCHET[ratchet_at(seq_ed(), 1u)] == 2u, "ROLL, step 2's key: a hit, ROLL 2");
    for (i = 0; i < 5u; i++) ui_drum_key(2);
    CHECK(ratchet_at(seq_ed(), 1u) == 0 && drum_at(seq_ed(), 2u, 1u) == 1u, "... five more: OFF again, the hit stays");
    hat_roll(1u, 3u);
    drum_put(seq_ed(), 2u, 1u, 0);
    CHECK(ratchet_at(seq_ed(), 1u) == 0, "the hit taken away: its ratchet too");

    {                                           /* a sequencer of version 3 (1.3's): its patterns, no ratchets */
        static uint8_t v3[SEQS_V3_SIZE];
        uint32_t p;
        seqs_init();
        seqs.pat[2].note[5] = 61;
        drum_put(&seqs.pat[3], 2u, 7u, 2u);
        seqs.dr_flt[2] = 99;
        seqs.version = 3;
        memcpy(v3, &seqs, 4);
        for (p = 0; p < SEQ_PATS; p++) {        /* (its pattern: as now up to the drums, then three drums' hits, accents) */
            uint8_t *o = v3 + 4u + p * SEQS_V3_PAT;
            memcpy(o, &seqs.pat[p], SEQS_V2_PAT);
            memcpy(o + SEQS_V2_PAT, seqs.pat[p].dhit, 24);
            memcpy(o + SEQS_V2_PAT + 24, seqs.pat[p].dacc, 24);
        }
        {                                       /* (its tail: div .. song_rep, then the kit of three) */
            uint8_t *t = v3 + 4u + SEQ_PATS * SEQS_V3_PAT;
            memcpy(t, &seqs.div, SEQS_TAIL);
            t += SEQS_TAIL;
            memcpy(t, seqs.dr_lvl, 3); memcpy(t + 3, seqs.dr_tune, 3); memcpy(t + 6, seqs.dr_dec, 3);
            t[9] = seqs.dr_drive; t[10] = seqs.dr_crush; t[11] = seqs.dr_dly; t[12] = seqs.dr_rev;
            memcpy(t + 13, seqs.dr_flt, 3);
        }
        seqstore_save(5, v3, sizeof v3);
        memset(&seqs_next, 0x5A, sizeof seqs_next);
        ok = seqs_fetch(5) == 0 && seqs_next.version == SEQS_VERSION && seqs_next.pat[2].note[5] == 61 &&
             drum_at(&seqs_next.pat[3], 2u, 7u) == 2u && seqs_next.dr_flt[2] == 99 && seqs_next.dr_ramp == 64;
        for (p = 0; ok && p < SEQ_PATS; p++)
            for (i = 0; i < 64u; i++) ok &= ratchet_at(&seqs_next.pat[p], i) == 0 && seqs_next.pat[p].len == 16u;
        CHECK(ok, "a slot's version 3 sequencer: read, its notes, drums and kit, no ratchets, RAMP EVEN");
        {                                       /* the store as 1.3 left it: the bank, then the version 3 sequencer */
            static uint8_t st[BANK_SIZE(PATCH_V13_SIZE) + SEQS_V3_SIZE];
            patch_t c = PATCH_INIT;
            c.split = 60;
            memset(st, 0, sizeof st);           /* (its bank: format 13) */
            st[0] = 13;
            st[4u + 1u] = 1;
            memcpy(st + BANK_SIZE(0) + PATCH_V13_SIZE, &c, PATCH_V13_SIZE);
            memcpy(st + BANK_SIZE(PATCH_V13_SIZE), v3, sizeof v3);
            store_save(st, sizeof st);
            memset(&bank, 0, sizeof bank);
            memset(&seqs, 0, sizeof seqs);
            patches_load();
            CHECK(bank.used[1] && bank.p[1].split == 60 && seqs.version == SEQS_VERSION && seqs.pat[2].note[5] == 61 &&
                  seqs.dr_ramp == 64, "the store saved by 1.3: its bank and its sequencer read");
        }
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
