/* SPDX-License-Identifier: GPL-3.0-only */
/* 1.4's features on a computer, through the app's own code: a step's lock (only its step's notes; a send's lock, only
 * its step), the mod matrix (a route heard, an empty one not), MOD FX and CRUSH (off: nothing changed; on: changed),
 * the kits (the clap's lane; each kit its own sound), the looper (a loop recorded from the first note, played back,
 * cleared) and UNDO (a pattern cleared and back; again: redo).
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

#define STEP_F 5512u                            /* a 1/16 at 120 BPM: 5512.5 frames */
static int32_t buf[2u * BLK];

static uint64_t run(uint32_t n, uint32_t from)  /* n frames on; the energy of those from frame `from` on */
{
    uint64_t e = 0;
    uint32_t i, at = 0;
    for (; n >= BLK; n -= BLK, at += BLK) {
        engine_render(buf, BLK);
        if (at >= from)
            for (i = 0; i < 2u * BLK; i++) e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
    }
    return e;
}

static void fresh(void)                         /* the default sound at 120 BPM, the sequencer cleared, stopped (and */
{                                               /* from its first step at the next PLAY) */
    engine_seq_play(0);
    run(BLK, 0);
    seqs_init();
    edit = PATCH_INIT;
    edit.arp_bpm = 80;
    engine_init();
    engine_set(&edit);
    engine_sync(0);
}

static uint64_t apart(uint64_t (*f)(uint32_t), uint32_t arg)   /* f(arg) in a process of its own: from power-on */
{                                               /* each time (engine_init leaves the voices, the effects as they are) */
    int fd[2];
    uint64_t r = 0;
    pid_t c;
    if (pipe(fd)) return ~0ull;
    fflush(stdout);
    if (!(c = fork())) {
        r = f(arg);
        if (write(fd[1], &r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    if (read(fd[0], &r, sizeof r) != sizeof r) r = ~0ull;
    waitpid(c, 0, 0);
    close(fd[0]); close(fd[1]);
    return r;
}

static void two_notes(void)                     /* P1: a note on steps 1 and 2 */
{
    seqs.pat[0].trig[0] = seqs.pat[0].trig[1] = SEQ_NOTE;
    seqs.pat[0].note[0] = seqs.pat[0].note[1] = 48;
}

static uint64_t lock_case(uint32_t i)            /* the steps' energy: i bit 0, step 2's cutoff locked; bit 1: step 2 */
{                                               /* (else step 1) */
    uint64_t e0, e1;
    fresh(); two_notes();
    if (i & 1u) {
        seqs.pat[0].lk_dest[0] = LK_CUTOFF;
        seqs.pat[0].lk[0][1] = 1u + 8u;
    }
    engine_seq_play(1);
    e0 = run(STEP_F - 400u, 0);
    e1 = run(STEP_F, 0);
    return i & 2u ? e1 : e0;
}

static uint64_t fx_case(uint32_t i)              /* a note's energy: i 0 the default; 1 MOD FX MIX 0, CRUSH off; 2, 3 */
{                                               /* the phaser, the flanger; 4 CRUSH */
    fresh();
    if (i == 1u) { edit.mfx_mix = 0; edit.mfx_type = MFX_PHASER; }
    if (i == 2u || i == 3u) {
        edit.mfx_type = (uint8_t)(i - 1u); edit.mfx_mix = 127; edit.mfx_depth = 127; edit.mfx_fb = 120;
    }
    if (i == 4u) { edit.crush_bits = 127; edit.crush_rate = 127; }
    engine_set(&edit);
    engine_note_on(48);
    return run(20000, 4000);
}

int main(void)
{
    uint64_t a0, a1, b0, b1, x, y;
    uint32_t k, ok;
    printf("1.4 feature checks\n");

    /* ---- locks: step 2's cutoff, low: step 1 as it was, step 2 darker */
    a0 = apart(lock_case, 0); b0 = apart(lock_case, 1);
    a1 = apart(lock_case, 2); b1 = apart(lock_case, 3);
    CHECK(a0 == b0, "a lock: the step before it as it was");
    CHECK(b1 < a1 / 2u, "... its own step's note: the cutoff locked low (%llu vs %llu)", (unsigned long long)b1,
          (unsigned long long)a1);
    fresh(); two_notes();
    seqs.pat[0].lk_dest[1] = LK_DSEND;
    seqs.pat[0].lk[1][1] = 1u + 100u;
    engine_seq_play(1);
    run(STEP_F - 400u, 0);
    x = (uint64_t)(lk_send[0] + 2);
    run(STEP_F, 0);
    y = (uint64_t)(lk_send[0] + 2);
    run(STEP_F, 0);
    CHECK(x == 1u && y == 102u && lk_send[0] == -1 && lk_send[1] == -1,   /* (-1: the sound's own) */
          "a send's lock: its step alone (%d before, %d on it, %d after)", (int)x - 2, (int)y - 2, (int)lk_send[0]);
    engine_seq_play(0);

    /* ---- the matrix: velocity onto the cutoff, down: a soft note darker than a hard one; route OFF: the same */
    fresh();
    edit.mm_src[0] = MM_VEL; edit.mm_dest[0] = DEST_CUTOFF; edit.mm_amt[0] = 0;
    engine_set(&edit);
    CHECK(eg.mm_n == 1u, "the matrix: one route packed");
    engine_note_on_vel(48, 127); a0 = run(8000, 2000); engine_note_off(48); run(30000, 0);
    engine_note_on_vel(48, 20); a1 = run(8000, 2000); engine_note_off(48); run(30000, 0);
    edit.mm_src[0] = MM_OFF;
    engine_set(&edit);
    CHECK(eg.mm_n == 0u, "... OFF: none");
    engine_note_on_vel(48, 127); b0 = run(8000, 2000); engine_note_off(48); run(30000, 0);
    CHECK(a0 < a1 && a1 < b0, "VELO -> CUTOFF (-64): a hard note darker than a soft one (%llu vs %llu; none %llu)",
          (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)b0);

    /* ---- MOD FX, CRUSH: off, the sound as it was; on, changed */
    a0 = apart(fx_case, 0);
    CHECK(a0 == apart(fx_case, 1), "MOD FX's MIX 0, CRUSH off: the sound bit for bit as without");
    for (k = MFX_PHASER; k <= MFX_FLANGER; k++) {
        b0 = apart(fx_case, k + 1u);
        CHECK(b0 != a0 && b0 > 0u, "%s: the sound changed", MFX_NAME[k]);
    }
    b0 = apart(fx_case, 4);
    CHECK(b0 != a0 && b0 > 0u, "CRUSH: the sound changed");

    /* ---- the kits: the clap's lane plays the clap; each kit its own sound */
    for (k = 0, ok = 1; k < DR_KITS; k++) ok &= DR_KIT[k][DR_CLAP_S].n > 0u;
    CHECK(DR_KITS >= 3u && ok, "%u kits, each with a clap", (unsigned)DR_KITS);
    CHECK(DR_KIT[0][DR_KICK_S].d != DR_KIT[1][DR_KICK_S].d && DR_KIT[1][DR_KICK_S].wide,
          "each kit its own sounds (TR: 16 bits)");
    fresh();
    drum_put(&seqs.pat[0], 3, 0, 1);
    engine_seq_play(1);
    run(2u * BLK, 0);
    CHECK(drv[DR_VOICE(DR_CLAP_S)].d == DR_KIT[0][DR_CLAP_S].d, "the clap's lane: the clap, LO-FI's");
    engine_seq_play(0);
    fresh();
    seqs.dr_kit = 2;
    drum_put(&seqs.pat[0], 0, 0, 1);
    engine_seq_play(1);
    run(2u * BLK, 0);
    CHECK(drv[DR_VOICE(DR_KICK_S)].d == DR_KIT[2][DR_KICK_S].d, "KIT BOOM: its kick");
    seqs.dr_lvl[3] = 0;
    engine_seq_play(0);

    /* ---- the DRUM page's keys: C#4 the clap (its LED); F#4 / G#4 the 16 steps before / after */
    fresh();
    page = PG_DRUM;
    seqs.pat[0].len = 32;
    dr_cursor = 0;
    ui_drum_key(8);
    CHECK(dr_sel == 3u && ui_drum_leds() >> 8 & 1u, "C#4: the clap, its key lit");
    ui_drum_key(14);                            /* (A4: step 9) */
    CHECK(drum_at(&seqs.pat[0], 3, 8) == 1u, "... a white key: a clap on its step");
    ui_drum_key(15);
    CHECK(dr_cursor / 16u == 1u, "G#4: the 16 after");
    ui_drum_key(13);
    CHECK(dr_cursor / 16u == 0u && dr_sel == 3u, "F#4: the 16 before (the drum stays)");

    /* ---- the looper: REC, a note at once (stopped: from the first note), 1 bar; played back */
    fresh();
    lp_bars = 1;
    loop_cmd(LPC_REC);
    run(BLK, 0);
    CHECK(lp.state == LP_ARMED && lp.rec, "the looper: REC, armed");
    loop_note(60, 100);
    engine_note_on(60);
    run(4000, 0);
    CHECK(lp.state == LP_REC && lp.run, "... a note: recording");
    loop_note(60, 0);
    engine_note_off(60);
    run(4u * 4u * STEP_F + 2000u, 0);           /* (past the bar's end: the loop goes round) */
    CHECK(lp.n == 2u && lp.ev[0].note == 60 && lp.ev[0].vel == 100 && !lp.ev[1].vel && lp.ev[1].t > lp.ev[0].t,
          "... the bar over: its note on and off, kept (%u events)", (unsigned)lp.n);
    loop_cmd(LPC_RECOFF);
    engine_all_off();
    run(4u * 4u * STEP_F - 6000u, 0);           /* (to just before the note, round again) */
    x = run(6000u, 3000u);                      /* (the note played back) */
    CHECK(lp.state == LP_PLAY && !lp.rec && lp.run && x > 0u, "REC off: played back, round again");
    loop_cmd(LPC_CLEAR);
    run(30000, 0);
    for (k = 0, ok = 1; k < 4u * 4u * STEP_F; k += BLK) {   /* (a round: no note of the loop's starts) */
        run(BLK, 0);
        for (x = 0; x < 128u; x++) ok &= lp.v[x] < 0;
    }
    CHECK(lp.state == LP_EMPTY && !lp.n && !lp.run && ok, "CLEAR: empty, no note played");

    /* ---- UNDO: a pattern cleared, back; again: cleared (redo) */
    fresh();
    two_notes();
    page = PG_SEQ;
    seq_row = 4;                                /* (the EDIT row's ROW: ALL) */
    ud_take("CLEAR");
    memset(&seqs.pat[0].trig, 0, sizeof seqs.pat[0].trig);
    pa_row = PA_UNDO;
    pa_do();
    CHECK(seqs.pat[0].trig[0] == SEQ_NOTE && seqs.pat[0].trig[1] == SEQ_NOTE && ud_undone, "UNDO: the notes back");
    pa_do();
    CHECK(seqs.pat[0].trig[0] == SEQ_REST && !ud_undone, "UNDO again: cleared again (a redo)");
    edit.cut = 30;
    ud_ok = 0;
    ud_mark("EDIT");
    edit.cut = 31;
    ud_mark("EDIT");                            /* (no pause: the same step) */
    edit.cut = 90;
    pa_do();
    CHECK(edit.cut == 30, "a knob's turn, at once again: one step back to where it was (CUTOFF %u)", (unsigned)edit.cut);

    /* ---- locks: with the steps through COPY, PASTE (a lane OFF: the copy's parameter) and CLEAR */
    fresh();
    page = PG_SEQ;
    seqs.pat[0].lk_dest[1] = LK_FM1;
    seqs.pat[0].lk[1][3] = 1u + 90u;
    seqs.cur = 0; seq_row = 0;
    seq_action(K_SCOPY);
    seqs.cur = 1; seq_row = 2;
    seq_action(K_SPASTE);
    CHECK(seqs.pat[1].lk[1][35] == 91u && seqs.pat[1].lk_dest[1] == LK_FM1 && !seqs.pat[1].lk[1][3],
          "a row's locks: pasted with its steps, the lane's parameter with them");
    seq_action(K_SCLEAR);
    CHECK(!seqs.pat[1].lk[1][35] && seqs.pat[0].lk[1][3] == 91u, "CLEAR: the row's locks gone (the copy's kept)");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
