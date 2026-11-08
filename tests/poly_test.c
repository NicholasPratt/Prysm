/* SPDX-License-Identifier: GPL-3.0-only */
/* Polymetre on a computer, through the app's own code and the engine: in DUAL, part B's notes looping a length of their
 * own (the POLY row's B LEN) against the pattern's (part A's, the drums'): 4 against 3 lines up again after 12 steps;
 * B longer than A plays its steps past A's length; a tie on B holds over B's own steps; SONG mode starts both at each
 * round; SINGLE, or B LEN as A's, plays as before; REC puts a part B note on B's own step; a whole pattern's copy
 * takes both lengths.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static uint64_t got[2][64][2];                  /* the notes each step started, by part (a bit a note) */
static uint32_t got_n, last_age;

static void block(void)                          /* a render of a block; the notes started in it, by part, by step */
{
    static int32_t blk[2u * BLK];
    uint32_t v, was = seq.steps, top = last_age;
    engine_render(blk, BLK);
    if (seq.steps != was && got_n < 64u) got_n++;   /* (a step started: the notes now are its) */
    for (v = 0; v < NV; v++)
        if (voice[v].age > last_age) {
            if (got_n) got[voice[v].part & 1u][got_n - 1u][voice[v].note >> 6 & 1u] |= 1ull << (voice[v].note & 63u);
            if (voice[v].age > top) top = voice[v].age;
        }
    last_age = top;
}
static void steps(uint32_t n)                    /* the sequencer n steps on */
{
    uint32_t to = seq.steps + n;
    while (seq.steps < to) block();
}
static int has(uint32_t part, uint32_t k, uint32_t note)   /* step k (from 0) started note on part */
{
    return got[part][k][note >> 6 & 1u] >> (note & 63u) & 1u;
}
static uint32_t count(uint32_t part, uint32_t k)   /* the notes step k started on part */
{
    return (uint32_t)(__builtin_popcountll(got[part][k][0]) + __builtin_popcountll(got[part][k][1]));
}

static void start(uint32_t vmode)                /* the sound (DUAL or SINGLE), 120 BPM, 1/16, stopped */
{
    static int32_t blk[2u * BLK];
    uint32_t i;
    engine_seq_play(0);
    for (i = 0; i < 8u; i++) engine_render(blk, BLK);
    engine_all_off();
    edit = PATCH_INIT;
    edit.cho_mix = edit.dly_mix = edit.rev_mix = 0;
    edit.vmode = (uint8_t)vmode;
    edit.arp_bpm = 80;                           /* (120 BPM) */
    engine_init();
    engine_set(&edit);
    engine_sync(0);
    seqs_init();
    memset(got, 0, sizeof got);
    got_n = 0;
    last_age = ages;
}
static void run_seq(uint32_t n)                     /* PLAY, n steps; got_*: what each step started */
{
    engine_seq_play(1);
    steps(n);
}
static void put_a(pattern_t *p, uint32_t k, uint32_t note)   /* lane 0 of step k: a note on part A */
{
    p->note[k] = (uint8_t)note; p->trig[k] = SEQ_NOTE;
    p->lpart[k] &= (uint8_t)~1u;
}
static void put_b(pattern_t *p, uint32_t k, uint32_t note, uint32_t tie)   /* lane 1 of step k: part B's (a tie) */
{
    p->xnote[k][0] = (uint8_t)(note | (tie ? SEQ_XTIE : 0u)); p->xvel[k][0] = 100;
    p->lpart[k] |= 2u;
}

int main(void)
{
    pattern_t *p = &seqs.pat[0];
    uint32_t i, ok;
    printf("POLYMETRE\n");

    /* 4 against 3: A's notes 60 .. 63 on steps 1 .. 4, B's 72 .. 74 on steps 1 .. 3, B LEN 3 */
    start(VM_DUAL);
    p->len = 4; p->blen = 3;
    for (i = 0; i < 4u; i++) put_a(p, i, 60u + i);
    for (i = 0; i < 3u; i++) put_b(p, i, 72u + i, 0);
    run_seq(13);
    for (i = 0, ok = 1; i < 12u; i++) ok &= has(0, i, 60u + i % 4u) && count(0, i) == 1u && has(1, i, 72u + i % 3u) && count(1, i) == 1u;
    CHECK(ok, "A LEN 4, B LEN 3: A round 4 steps, B round 3, over 12 steps");
    CHECK(has(0, 12, 60) && has(1, 12, 72), "... and both on their first step again at the 13th (they line up)");
    CHECK(pat_poly(p, VM_DUAL) && !pat_poly(p, VM_SINGLE), "polymetre: DUAL only");

    /* B longer than A: A LEN 2, B LEN 5; B's notes past A's length play */
    start(VM_DUAL);
    p->len = 2; p->blen = 5;
    put_a(p, 0, 48); put_a(p, 1, 50);
    for (i = 0; i < 5u; i++) put_b(p, i, 80u + i, 0);
    run_seq(10);
    for (i = 0, ok = 1; i < 10u; i++) ok &= has(0, i, i % 2u ? 50u : 48u) && has(1, i, 80u + i % 5u);
    CHECK(ok, "A LEN 2, B LEN 5: B plays its steps 3 .. 5 (past A's length), A round its 2");
    CHECK(seq.playing_b == 4u && seq.playing == 1u, "the steps playing: A's 2, B's 5 (the page's marks)");

    /* a tie on B: its note held over B's next step, no new start; the tie on B's step 1 (after its wrap) */
    start(VM_DUAL);
    p->len = 4; p->blen = 3;
    put_b(p, 0, 70, 0);
    put_b(p, 1, 70, 1);                          /* (step 2: the tie) */
    run_seq(7);
    CHECK(has(1, 0, 70) && !count(1, 1) && !count(1, 2) && has(1, 3, 70) && !count(1, 4) && has(1, 6, 70),
          "B's note on its step 1, tied over its step 2: one start a round of 3 (steps 1, 4, 7)");

    /* SONG: P1 twice; each round starts both (B too, from its first step) */
    start(VM_DUAL);
    p->len = 4; p->blen = 3;
    for (i = 0; i < 4u; i++) put_a(p, i, 60u + i);
    for (i = 0; i < 3u; i++) put_b(p, i, 72u + i, 0);
    seqs.mode = 1; seqs.song_len = 1; seqs.song_pat[0] = 0; seqs.song_rep[0] = 2;
    run_seq(8);
    CHECK(has(1, 0, 72) && has(1, 3, 72) && has(1, 4, 72) && has(1, 5, 73) && has(1, 7, 72),
          "SONG: B round its 3, then from its first step again as the pattern starts again");

    /* SINGLE (B LEN set, not DUAL): one length, as before */
    start(VM_SINGLE);
    p->len = 4; p->blen = 3;
    for (i = 0; i < 4u; i++) put_a(p, i, 60u + i);
    for (i = 0; i < 3u; i++) put_b(p, i, 72u + i, 0);
    run_seq(8);
    for (i = 0, ok = 1; i < 8u; i++)
        ok &= has(0, i, 60u + i % 4u) && (i % 4u < 3u ? has(0, i, 72u + i % 4u) : count(0, i) == 1u) && !count(1, i);
    CHECK(ok, "SINGLE: B LEN ignored, every lane round the pattern's 4 (on part A)");

    /* B LEN as A's (0, or the same): DUAL as before */
    start(VM_DUAL);
    p->len = 4; p->blen = 4;
    for (i = 0; i < 4u; i++) put_a(p, i, 60u + i);
    for (i = 0; i < 3u; i++) put_b(p, i, 72u + i, 0);
    run_seq(8);
    for (i = 0, ok = !pat_poly(p, VM_DUAL); i < 8u; i++)
        ok &= has(0, i, 60u + i % 4u) && (i % 4u < 3u ? has(1, i, 72u + i % 4u) : !count(1, i));
    CHECK(ok, "DUAL, B LEN as A's: both round the pattern's 4, as before");

    /* REC: a key on part B lands on B's own step (A's 2nd step, B's 3rd) */
    start(VM_DUAL);
    p->len = 4; p->blen = 3;
    seq_rec = 1;
    run_seq(6);                                     /* (steps 1 .. 6 started: A on its 2nd, B on its 3rd) */
    for (i = 0; i < 4u; i++) block();            /* (early in it) */
    CHECK(seq.playing == 1u && seq.playing_b == 2u, "6 steps in: A on its step 2, B on its step 3");
    ui_seq_key(67, 100, 1);
    ui_seq_key(55, 100, 0);
    steps(1);
    ui_seq_key_up(67);
    ui_seq_key_up(55);
    {
        uint32_t nn, nv, l, onb = 0, ona = 0;
        for (l = 0; l < SEQ_LANES; l++) {
            if (seq_lane(p, 2, l, &nn, &nv) == SEQ_NOTE && nn == 67u && p->lpart[2] >> l & 1u) onb = 1;
            if (seq_lane(p, 1, l, &nn, &nv) == SEQ_NOTE && nn == 55u && !(p->lpart[1] >> l & 1u)) ona = 1;
        }
        CHECK(onb && ona, "REC: the part B key onto B's step 3, the part A key onto A's step 2");
    }
    seq_rec = 0;

    {                                            /* a whole pattern copied (EDIT, ROW ALL): both lengths go with it */
        start(VM_DUAL);
        p->len = 12; p->blen = 7;
        page = PG_SEQ;
        seq_row = 4;
        seqs.cur = 0;
        seq_action(K_SCOPY);
        seqs.cur = 1;
        seq_action(K_SPASTE);
        CHECK(seqs.pat[1].len == 12u && seqs.pat[1].blen == 7u, "a pattern copied: its A LEN 12 and B LEN 7 too");
        seqs.cur = 0;
    }
    engine_seq_play(0);
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
