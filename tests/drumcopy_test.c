/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRUM page's EDIT row on a computer, through the app's own code (ui.c): COPY and PASTE of the drums' lanes
 * between patterns, the notes and the length left as they are; a 16-step beat repeated over 32; one lane onto
 * another (a flam off the snare: an accent; a roll only on the hat); a lane pasted with LANE ALL: into its own. The
 * KIT row's preset beats: each well formed; one loaded (16 steps repeated, the notes kept), then UNDO.
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

    /* ---- the KIT row's preset beats */
    for (k = 0, ok = 1; k < DPRE_N; k++) {
        uint32_t d, i;
        for (d = 0; d < DRUMS; d++) {
            const char *l = DPRE[k].lane[d];
            ok &= strlen(l) == 16u && strlen(DPRE[k].name) <= 8u;
            for (i = 0; i < 16u; i++)
                ok &= l[i] == '.' || l[i] == 'x' || l[i] == 'X' || l[i] == 'g' || (l[i] == 'f' && d == 1u) ||
                      (DR_ROLLS(d) && (l[i] == '2' || l[i] == '3' || l[i] == '4' || l[i] == '6' || l[i] == '8'));
        }
        ok &= DPRE[k].lane[0][0] == 'x';        /* (each starts on a kick) */
    }
    CHECK(DPRE_N >= 24u && ok, "%u preset beats: 16 steps a lane, names of 8 or less, only the lane's own marks",
          (unsigned)DPRE_N);
    seqs.cur = 1;                               /* P2: 32 steps, its note on step 6 */
    for (k = 0; k < DPRE_N && strcmp(DPRE[k].name, "TRAP 2"); k++) ;
    dr_pre = (uint8_t)k;
    {
        static pattern_t was;
        was = *p2;
        drum_preset();
        CHECK(drum_at(p2, 0, 0) == 1u && drum_at(p2, 1, 8) == 1u && drum_at(p2, 3, 8) == 1u && drum_at(p2, 1, 4) == 0u &&
              ratchet_at(p2, 3) == 1u && ratchet_at(p2, 14) == 4u && ratchet_at(p2, 2) == 0u,
              "TRAP 2 loaded: kick, snare, clap, the hat's rolls (2 and 6 hits)");
        CHECK(same(p2, 0, p2, 0, 0, 16, 16) && same(p2, 2, p2, 2, 0, 16, 48) && ratchet_at(p2, 30) == 4u,
              "... 16 steps, repeated over the 32 (and on to 64)");
        CHECK(p2->len == 32u && p2->trig[5] == SEQ_NOTE && p2->note[5] == 67, "... the notes and the length stay");
        ud_swap();
        CHECK(!memcmp(&was, p2, sizeof was), "UNDO: the pattern as it was");
    }

    /* ---- ghosts: a soft hit, a state of its own (its own bit), in the presses' round */
    {
        pattern_t *g = &seqs.pat[3];
        uint32_t h, d, n;
        memset(g, 0, sizeof *g);
        g->len = 16;
        for (d = 0, ok = 1; d < DRUMS; d++)
            for (h = DH_NONE; h < DH_N; h++) {
                if (h == DH_FLAM && d != 1u) continue;
                drum_put(g, d, 9, h);
                ok &= drum_at(g, d, 9) == h && drum_at(g, d, 8) == DH_NONE && drum_at(g, d, 10) == DH_NONE;
            }
        CHECK(ok, "every drum: none, hit, accent, (the snare's) flam, ghost each put and read back, its neighbours none");
        drum_put(g, 1, 5, DH_GHOST);
        drum_put(g, 1, 5, DH_ACCENT);
        CHECK(drum_at(g, 1, 5) == DH_ACCENT && !(g->dgho[1][0] >> 5 & 1u), "a ghost overwritten: its bit gone");
        for (h = DH_NONE, n = 0; n < 5u; n++) h = drum_next(1, h);
        CHECK(drum_next(1, DH_NONE) == DH_HIT && drum_next(1, DH_ACCENT) == DH_FLAM && drum_next(1, DH_FLAM) == DH_GHOST &&
              drum_next(1, DH_GHOST) == DH_NONE && h == DH_NONE, "the snare's round: hit, accent, flam, ghost, none");
        CHECK(drum_next(0, DH_ACCENT) == DH_GHOST && drum_next(2, DH_ACCENT) == DH_GHOST && drum_next(3, DH_GHOST) == DH_NONE,
              "the others': hit, accent, ghost, none (no flam)");
        CHECK(drum_vel(DH_GHOST) < drum_vel(DH_HIT) && drum_vel(DH_HIT) < drum_vel(DH_ACCENT), "a ghost plays softest");
        seqs.dr_ghost = 20;
        CHECK(drum_vel(DH_GHOST) == 20u && drum_vel(DH_HIT) == DR_HIT_VEL, "the KIT row's GHOST: a ghost's velocity");
        seqs.dr_ghost = DR_GHOST_VEL;
        for (k = 0; k < DPRE_N && strcmp(DPRE[k].name, "FUNK"); k++) ;
        dr_pre = (uint8_t)k;
        seqs.cur = 3;
        drum_preset();
        CHECK(k < DPRE_N && drum_at(g, 1, 2) == DH_GHOST && drum_at(g, 1, 4) == DH_HIT && drum_at(g, 1, 18) == DH_GHOST &&
              drum_at(g, 2, 14) == DH_ACCENT, "FUNK loaded: the snare's ghosts (g), its backbeat, repeated");
        dr_lane = 2;                            /* the snare's ghosts onto the hat: ghosts there too */
        drum_copy(0);
        dr_lane = 3;
        drum_copy(1);
        CHECK(drum_at(g, 2, 2) == DH_GHOST && drum_at(g, 2, 4) == DH_HIT, "the SNARE onto the HAT: its ghosts stay ghosts");
        for (k = 0; k < DPRE_N && strcmp(DPRE[k].name, "TRAP RLL"); k++) ;
        dr_pre = (uint8_t)k;
        drum_preset();
        CHECK(k < DPRE_N && drum_at(g, 1, 14) == DH_HIT && RATCHET[roll_at(g, 1, 14)] == 4u &&
              RATCHET[roll_at(g, 2, 11)] == 2u && roll_at(g, 1, 11) == 0, "TRAP RLL loaded: the snare's roll of 4, the hat's of 2");
        dr_lane = 2;                            /* the snare's roll onto the hat, onto the kick */
        drum_copy(0);
        dr_lane = 3;
        drum_copy(1);
        CHECK(RATCHET[roll_at(g, 2, 14)] == 4u && roll_at(g, 2, 11) == 0, "the SNARE onto the HAT: its roll rolls there");
        dr_lane = 1;
        drum_copy(1);
        CHECK(drum_at(g, 0, 14) == DH_HIT && roll_at(g, 0, 14) == 0, "... onto the KICK: the hit, no roll");
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
