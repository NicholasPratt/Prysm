/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRUM page's EDIT row on a computer, through the app's own code (ui.c): COPY and PASTE of the drums' lanes
 * between patterns, the notes and the length left as they are; a 16-step beat repeated over 32; one lane onto
 * another (a flam off the snare: an accent; a roll only on the hat); a lane pasted with LANE ALL: into its own.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int same(const pattern_t *a, uint32_t da, const pattern_t *b, uint32_t db, uint32_t from, uint32_t n, uint32_t at)
{                                               /* lane da of a's steps from .. as lane db of b's from at */
    uint32_t k;
    for (k = 0; k < n; k++)
        if (drum_at(a, da, from + k) != drum_at(b, db, at + k)) return 0;
    return 1;
}

int main(void)
{
    pattern_t *p1 = &seqs.pat[0], *p2 = &seqs.pat[1], *p3 = &seqs.pat[2];
    uint32_t k, ok;
    printf("drum copy checks\n");
    seqs_init();
    edit = PATCH_INIT;
    engine_init();
    engine_set(&edit);
    page = PG_DRUM;
    for (k = 0; k < 16u; k += 4u) drum_put(p1, 0, k, 1u);   /* P1: a beat, 16 steps */
    drum_put(p1, 1, 4, 1u);
    drum_put(p1, 1, 12, 2u);
    drum_put(p1, 1, 14, 3u);                    /* (a flam) */
    for (k = 0; k < 16u; k += 2u) drum_put(p1, 2, k, k == 2u ? 2u : 1u);
    ratchet_put(p1, 6, 3u);
    p2->len = 32;                               /* P2: 32 steps, a note of its own */
    p2->trig[5] = SEQ_NOTE;
    p2->note[5] = 67;

    seqs.cur = 1;
    drum_copy(1);
    CHECK(!drum_at(p2, 0, 0), "PASTE before any COPY: nothing");

    seqs.cur = 0;
    dr_lane = 0;                                /* ALL */
    drum_copy(0);
    seqs.cur = 1;
    drum_copy(1);
    for (k = ok = 0; k < DRUMS; k++) ok += same(p1, k, p2, k, 0, 16, 0) && same(p1, k, p2, k, 0, 16, 16);
    CHECK(ok == DRUMS, "ALL from P1 (16 steps) into P2 (32): the beat twice over");
    CHECK(ratchet_at(p2, 6) == 3u && ratchet_at(p2, 22) == 3u, "the hat's roll with it");
    CHECK(p2->len == 32u && p2->trig[5] == SEQ_NOTE && p2->note[5] == 67, "P2's notes and length as they were");
    CHECK(!drum_at(p2, 0, 32), "past its length: as it was");

    seqs.cur = 0;
    dr_lane = 1;                                /* KICK */
    drum_copy(0);
    seqs.cur = 2;
    dr_lane = 2;                                /* into SNARE */
    drum_copy(1);
    CHECK(same(p1, 0, p3, 1, 0, 16, 0) && !drum_at(p3, 0, 0) && !drum_at(p3, 2, 0),
          "P1's KICK into P3's SNARE: that lane alone");
    seqs.cur = 0;
    dr_lane = 2;                                /* SNARE */
    drum_copy(0);
    seqs.cur = 2;
    dr_lane = 1;                                /* into KICK */
    drum_copy(1);
    CHECK(drum_at(p3, 0, 14) == 2u && drum_at(p3, 0, 12) == 2u && drum_at(p3, 0, 4) == 1u,
          "the SNARE onto the KICK: its flam an accent");
    seqs.cur = 0;
    dr_lane = 3;                                /* HAT */
    drum_copy(0);
    seqs.cur = 2;
    dr_lane = 0;                                /* with ALL: into its own lane */
    drum_copy(1);
    CHECK(same(p1, 2, p3, 2, 0, 16, 0) && ratchet_at(p3, 6) == 3u, "the HAT with LANE ALL: into the hat, its roll too");
    CHECK(drum_at(p3, 0, 14) == 2u, "the other lanes as they were");
    seqs.cur = 1;
    dr_lane = 3;
    drum_copy(0);
    seqs.cur = 2;
    dr_lane = 1;                                /* the hat onto the kick: no roll anywhere new */
    drum_copy(1);
    CHECK(ratchet_at(p3, 6) == 3u && drum_at(p3, 0, 2) == 2u, "the HAT onto the KICK: the hat's roll stays the hat's");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
