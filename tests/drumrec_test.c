/* SPDX-License-Identifier: GPL-3.0-only */
/* REC on the drums on a computer, through the app's own code (ui.c) and the engine: while the sequencer plays, the DRUM
 * page's drum keys (F#3 G#3 A#3) record a hit into the nearest step; one into the step still to come is heard once,
 * not again from the pattern, which plays it on the next pass; an accent there stays one; REC off, nothing recorded.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

#define STEP_F 5512u                            /* a 1/16 at 120 BPM: 5512.5 frames */
#define KEY_SNARE 3u                            /* G#3 */

static uint32_t snares;                         /* the snare's starts, counted by run_frames */
static void run_frames(uint32_t n)              /* the engine, n frames on (renders of BLK, as the ISR) */
{
    static int32_t blk[2u * BLK];
    static const int8_t *was;
    static uint32_t prev;
    for (; n >= BLK; n -= BLK) {
        engine_render(blk, BLK);
        if (drv[1].d && (!was || drv[1].pos_q16 < prev)) snares++;
        was = drv[1].d;
        prev = drv[1].pos_q16;
    }
}

int main(void)
{
    pattern_t *p = &seqs.pat[0];
    uint32_t n;
    printf("drum REC checks\n");
    seqs_init();
    edit = PATCH_INIT;
    edit.arp_bpm = 80;                          /* 120 BPM */
    engine_init();
    engine_set(&edit);
    engine_sync(0);
    page = PG_DRUM;
    seq_rec = 1;
    engine_seq_play(1);
    run_frames(2u * BLK);                       /* early in step 1 */
    ui_drum_key(KEY_SNARE);
    CHECK(drum_at(p, 1, 0) == 1u && dr_sel == 1u, "G#3 early in step 1: a snare hit there, its lane chosen");
    run_frames(2u * STEP_F + STEP_F * 3u / 4u); /* step 3, past its middle: into step 4 */
    n = snares;
    ui_drum_key(KEY_SNARE);
    CHECK(drum_at(p, 1, 3) == 1u && drum_at(p, 1, 2) == 0u, "late in step 3: into step 4, step 3 left as it was");
    run_frames(STEP_F);                         /* through step 4 */
    CHECK(snares - n == 1u, "heard once as it was hit, not again from step 4 (%u)", (unsigned)(snares - n));
    drum_put(p, 1, 6, 2u);                      /* an accent on step 7 */
    run_frames(STEP_F * 2u + STEP_F / 4u);      /* early in step 7 */
    ui_drum_key(KEY_SNARE);
    CHECK(drum_at(p, 1, 6) == 2u, "a hit onto step 7's accent: it stays an accent");
    run_frames(STEP_F * 9u + STEP_F / 2u);      /* into step 16, then round to step 1 again and into step 4 */
    n = snares;
    run_frames(STEP_F * 4u);
    CHECK(snares - n == 2u, "the next pass: steps 1 and 4 play from the pattern (%u)", (unsigned)(snares - n));
    seq_rec = 0;
    run_frames(STEP_F * 4u + BLK);              /* step 9, REC off */
    ui_drum_key(KEY_SNARE);
    CHECK(drum_at(p, 1, 8) == 0u && drum_at(p, 1, 9) == 0u, "REC off: heard, not recorded");
    seq_rec = 1;
    engine_seq_play(0);
    run_frames(2u * BLK);
    ui_drum_key(KEY_SNARE);
    for (n = 0; n < 64u && (n == 0u || n == 3u || n == 6u || !drum_at(p, 1, n)); n++) ;
    CHECK(n == 64u, "stopped: nothing recorded (the snare on steps 1, 4, 7 alone)");

    for (n = 0; n < 64u; n++) { drum_put(p, 0, n, 0); drum_put(p, 1, n, 0); drum_put(p, 2, n, 0); }
    engine_seq_play(1);                         /* MIDI channel 10 while REC: by its velocity */
    run_frames(2u * BLK);
    ui_drum_midi(DR_SNARE_S, 30);
    run_frames(STEP_F);
    ui_drum_midi(DR_SNARE_S, 100);
    run_frames(STEP_F);
    ui_drum_midi(DR_SNARE_S, 127);
    run_frames(STEP_F);
    ui_drum_midi(DR_OHAT_S, 90);
    ui_drum_midi(DR_KICK_S, 60);
    CHECK(drum_at(p, 1, 0) == DH_GHOST && drum_at(p, 1, 1) == DH_HIT && drum_at(p, 1, 2) == DH_ACCENT,
          "MIDI snares at 30, 100, 127: a ghost, a hit, an accent");
    CHECK(drum_at(p, 2, 3) == DH_ACCENT && drum_at(p, 0, 3) == DH_GHOST, "the open hat: the hat's accent; a kick at 60: a ghost");
    seqs.dr_ghost = 90;                         /* GHOST up near a hit's: 60 is nearer the ghost still, 100 not */
    run_frames(STEP_F);
    ui_drum_midi(DR_SNARE_S, 60);
    run_frames(STEP_F);
    ui_drum_midi(DR_SNARE_S, 100);
    CHECK(drum_at(p, 1, 4) == DH_GHOST && drum_at(p, 1, 5) == DH_HIT, "GHOST at 90: a snare at 60 a ghost, at 100 a hit");
    seq_rec = 0;
    run_frames(STEP_F);
    ui_drum_midi(DR_SNARE_S, 30);
    CHECK(drum_at(p, 1, 6) == DH_NONE, "REC off: MIDI drums not recorded");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
