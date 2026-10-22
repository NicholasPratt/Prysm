/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer's REC on a computer, through the app's own code (ui.c) and the engine: real-time recording (a key
 * into the nearest step, its velocity, held: tied; a second key into another lane; the lane resting while a key
 * records into it) and step entry of a chord.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { printf("  %s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)

static void run_frames(uint32_t n)              /* the engine, n frames on (renders of 256, as the ISR) */
{
    static int32_t blk[2u * 256u];
    for (; n >= 256u; n -= 256u)
        engine_render(blk, 256u);
}
#define STEP_F 5512u                            /* a 1/16 at 120 BPM: 5512.5 frames */

int main(void)
{
    pattern_t *p = &seqs.pat[0];
    uint32_t nn, nv;
    printf("REC checks\n");
    seqs_init();
    edit = PATCH_INIT;
    edit.arp_bpm = 80;                          /* 120 BPM */
    engine_init();
    engine_set(&edit);
    engine_sync(0);
    seq_rec = 1;
    engine_seq_play(1);
    run_frames(256u);                           /* step 1 starts */
    run_frames(512u);                           /* early in step 1 */
    nn = (uint32_t)ui_seq_key(60, 90);
    ui_seq_key(60, 90);                         /* (a second note-on while it is held: nothing more) */
    CHECK(nn == 1u && seq.rec_mute == 1u, "a key early in step 1: recorded, its lane (0) resting");
    run_frames(2u * STEP_F);                    /* let go early in step 3 */
    ui_seq_key_up(60);
    CHECK(seq_lane(p, 0, 0, &nn, &nv) == SEQ_NOTE && nn == 60 && nv == 90, "step 1: C4 at velocity 90 (%u, %u)",
          (unsigned)nn, (unsigned)nv);
    CHECK(p->trig[1] == SEQ_TIE && p->trig[2] == SEQ_REST, "held into step 3: tied over step 2, step 3 free");
    CHECK(seq.rec_mute == 0u, "let go: its lane plays again");
    run_frames(STEP_F * 3u / 4u);               /* step 3, past its middle: the key goes into step 4 */
    ui_seq_key(64, 100);
    ui_seq_key(67, 100);                        /* and another with it: the next lane */
    run_frames(STEP_F / 2u);
    ui_seq_key_up(64);
    ui_seq_key_up(67);
    CHECK(seq_lane(p, 3, 0, &nn, &nv) == SEQ_NOTE && nn == 64 && seq_lane(p, 3, 1, &nn, &nv) == SEQ_NOTE && nn == 67,
          "late in step 3: E4 and G4 into step 4 (lanes 0, 1), a chord");
    CHECK(p->trig[2] == SEQ_REST, "step 3 left as it was");
    run_frames(STEP_F * 13u);                   /* round the pattern (16 steps) to step 1 again */
    run_frames(256u);
    ui_seq_key(60, 70);                         /* overdub: the same note into its own lane, a new one beside it */
    ui_seq_key(72, 70);
    run_frames(512u);
    ui_seq_key_up(60);
    ui_seq_key_up(72);
    CHECK(seq_lane(p, 0, 0, &nn, &nv) == SEQ_NOTE && nn == 60 && nv == 70 &&
          seq_lane(p, 0, 1, &nn, &nv) == SEQ_NOTE && nn == 72,
          "the second pass: C4 again in its lane (velocity 70), C5 added beside it");
    CHECK(p->trig[1] == SEQ_TIE, "the first pass's tie stays");
    seq_rec = 0;
    rec_end();
    engine_seq_play(0);
    run_frames(512u);
    seq_rec = 1;                                /* step entry: stopped, on the SEQ page */
    page = PG_SEQ;
    seq_cursor = 8;
    ui_seq_key(48, 127);
    ui_seq_key(52, 127);
    ui_seq_key(55, 127);
    CHECK(seq_cursor == 8, "a chord held: the cursor waits");
    ui_seq_key_up(52);
    ui_seq_key_up(48);
    ui_seq_key_up(55);
    CHECK(seq_lane(p, 8, 0, &nn, &nv) == SEQ_NOTE && nn == 48 && seq_lane(p, 8, 1, &nn, &nv) == SEQ_NOTE && nn == 52 &&
          seq_lane(p, 8, 2, &nn, &nv) == SEQ_NOTE && nn == 55 && seq_lane(p, 8, 3, &nn, &nv) == SEQ_REST,
          "step entry: C3 E3 G3 into step 9");
    CHECK(seq_cursor == 9, "all let go: the cursor on to step 10");
    ui_seq_key(50, 127);
    ui_seq_key_up(50);
    CHECK(seq_lane(p, 9, 0, &nn, &nv) == SEQ_NOTE && nn == 50 && seq_lane(p, 9, 1, &nn, &nv) == SEQ_REST,
          "a single key: a single note");
    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
