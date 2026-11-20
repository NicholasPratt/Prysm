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

static void run0(uint32_t n)                    /* n frames on, each block from silence (engine_render adds into out) */
{
    for (; n >= BLK; n -= BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
    }
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

    /* ---- DLYTIME from the matrix and the mod envelopes: the newest note's, onto the shared delay */
    {
        int32_t hard, soft, held, gone, menv, l2;
        uint32_t t0, t1;
        fresh();
        edit.dly_mix = 80; edit.dly_time = 20;
        edit.mm_src[0] = MM_VEL; edit.mm_dest[0] = DEST_DLYTIME; edit.mm_amt[0] = 127;
        engine_set(&edit);
        CHECK(eg.mm_n == 1u && eg.dly_mod, "the matrix: a route on DLYTIME packed");
        t0 = dly_time_q8;
        engine_note_on_vel(48, 127); run(4000, 0); hard = dly_mpz; engine_note_off(48); run(30000, 0);
        engine_note_on_vel(48, 30); run(4000, 0); soft = dly_mpz; t1 = dly_time_q8;
        engine_note_off(48); run(60000, 0); held = dly_mpz;
        CHECK(hard > 100 && soft > 0 && soft < hard && t1 > t0,
              "VELO -> DLYTIME: a hard note's time longer than a soft one's (%d vs %d knob steps; %u -> %u)",
              (int)hard, (int)soft, (unsigned)(t0 >> 8), (unsigned)(t1 >> 8));
        CHECK(held == soft, "... the notes gone: the delay's time as the last one left it (%d)", (int)held);
        edit.mm_src[0] = MM_OFF;
        engine_set(&edit);
        run(BLK, 0); gone = dly_mpz;
        CHECK(!eg.dly_mod && gone == 0, "... the route OFF: the knob's own time again (%d)", (int)gone);
        edit.menv_dest = DEST_DLYTIME; edit.menv_amt = 0; edit.menv_a = 0; edit.menv_s = 127;
        engine_set(&edit);
        engine_note_on(48); run(4000, 0); menv = dly_mpz; engine_note_off(48); run(30000, 0);
        CHECK(eg.dly_mod && menv < -100, "M.ENV 1 -> DLYTIME (-64): the time down while held (%d)", (int)menv);
        edit.menv_dest = DEST_OFF; edit.menv_amt = 64;
        edit.lfo2_dest = DEST_DLYTIME; edit.lfo2_amt = 127;
        engine_set(&edit);
        engine_note_on(48); run(4000, 0); l2 = dly_mpz; engine_note_off(48); run(30000, 0);
        CHECK(!eg.dly_mod && l2 == 0, "LFO 2 -> DLYTIME: fx_render's own, not counted twice (%d)", (int)l2);
    }

    /* ---- SYNC: the carrier hard-synced to mod 1 (a silent master at the note's pitch): the note's period kept
     * whatever the rate (the timbre moves); the same pitches in MIX are not periodic at the note. Mod 1's own wave
     * not heard; its AMT sweeps the rate up (3 octaves at full) */
    {
        static int32_t w[6][8192];
        static const struct { uint8_t md, rate, amt, mwave; } C[6] = {
            {MODE_SYNC, 72, 0, 72}, {MODE_SYNC, 124, 0, 72},   /* x2.83, x5.99: neither a whole number */
            {MODE_MIX, 18, 127, 72}, {MODE_MIX, 31, 127, 72},  /* (MIX: those as pitches, +-st from the note) */
            {MODE_SYNC, 124, 0, 0},                             /* mod 1 a sine, not a saw */
            {MODE_SYNC, 0, 127, 72}};                           /* x1, AMT full: swept, x1 .. x8, round again */
        double c[6];
        uint32_t q, j, n;
        for (q = 0; q < 6u; q++) {
            fresh();
            edit.osc[1].level = 0; edit.cho_mix = 0; edit.dly_mix = 0; edit.rev_mix = 0;
            edit.cut = 127; edit.res = 0; edit.fenv = 64; edit.amp_a = 0; edit.amp_s = 127;
            edit.osc[0].bank[0] = 0; edit.osc[0].pos[0] = 72;   /* (the carrier a saw: the classic sync sound) */
            edit.osc[0].mode[0] = C[q].md; edit.osc[0].amt[0] = C[q].amt; edit.osc[0].att[0] = 0;
            edit.osc[0].amt[1] = 0;
            edit.osc[0].bank[1] = 0; edit.osc[0].pos[1] = C[q].mwave;
            if (C[q].md == MODE_SYNC) edit.sync_rate[0][0] = C[q].rate; else edit.mpitch[0][0] = C[q].rate;
            engine_set(&edit);
            engine_note_on(57);                  /* A3: 220 Hz, 200.45 frames a cycle */
            run0(8192);
            for (n = 0; n < 8192u; n += BLK) {
                memset(buf, 0, sizeof buf);
                engine_render(buf, BLK);
                for (j = 0; j < BLK; j++) w[q][n + j] = buf[2u * j];
            }
            engine_note_off(57);
            run0(30000);
            {                                   /* the correlation one cycle apart (44100 / 220 frames: between two, */
                double sxy = 0, sxx = 0, syy = 0, L = 44100.0 / 220.0;   /* interpolated) */
                for (j = 0; j + 202u < 8192u; j++) {
                    double at = j + L, f = at - (double)(uint32_t)at;
                    double y = w[q][(uint32_t)at] + (w[q][(uint32_t)at + 1u] - w[q][(uint32_t)at]) * f;
                    sxy += (double)w[q][j] * y; sxx += (double)w[q][j] * w[q][j]; syy += y * y;
                }
                c[q] = sxx > 0 && syy > 0 ? sxy / __builtin_sqrt(sxx * syy) : 0;
            }
        }
        {
            uint32_t same = 1, d58 = 0;
            for (j = 0; j < 8192u; j++) same &= w[1][j] == w[4][j];
            for (j = 0; j < 8192u; j++) d58 += w[0][j] != w[1][j];
            CHECK(MODE_N == 4u && !strcmp(MODE_NAME[MODE_SYNC], "SYNC"), "MODE: FM, RING, MIX, SYNC");
            CHECK(c[0] > 0.95 && c[1] > 0.95 && d58 > 4000u, "SYNC x2.83, x5.99: the carrier at the note's period "
                  "(%.3f, %.3f), unlike each other", c[0], c[1]);
            CHECK(c[2] < 0.9 && c[3] < 0.9, "... MIX at those pitches: not (%.3f, %.3f)", c[2], c[3]);
            CHECK(same, "... the master not heard: mod 1 a sine or a saw, the same sound");
            CHECK(c[5] < c[0] - 0.05, "... AMT full: the rate swept by its envelope, the timbre moving cycle to cycle "
                  "(%.3f)", c[5]);
        }
        {                                       /* the modulator's envelope: SYNC an AD looping (in the release too); */
            uint32_t md, cyc[2], low[2], n;        /* FM its AR, held at the top */
            for (md = 0; md < 2u; md++) {
                int32_t was = 0, up = 0;
                fresh();
                edit.osc[0].mode[0] = md ? MODE_FM : MODE_SYNC; edit.osc[0].amt[0] = 127;
                edit.osc[0].att[0] = 40; edit.osc[0].rel[0] = 40; edit.amp_r = 127;
                engine_set(&edit);
                engine_note_on(57);
                cyc[md] = low[md] = 0;
                for (j = 0; j < 44100u / BLK; j++) {   /* a second held, then a second let go */
                    int32_t e;
                    if (j == 22050u / BLK) engine_note_off(57);
                    run0(BLK);
                    e = 0;
                    for (n = 0; n < NV; n++) e = voice[n].mod_q15[0][0] > e ? voice[n].mod_q15[0][0] : e;
                    if (j > 2000u / BLK && j < 22050u / BLK && e < 3000) low[md]++;
                    if (!up && e > 30000) { up = 1; cyc[md]++; }
                    else if (up && e < 3000) up = 0;
                    was = e;
                }
                (void)was;
                run0(60000);
            }
            CHECK(cyc[0] >= 4u, "SYNC: the modulator's envelope an AD, looping (%u peaks in 1 s)", (unsigned)cyc[0]);
            CHECK(cyc[1] == 1u && low[1] == 0u, "... FM: its AR, held at the top while the key is down (%u, %u)",
                  (unsigned)cyc[1], (unsigned)low[1]);
        }
        fresh();
        CHECK(edit.sync_rate[0][0] == 96u && edit.sync_rate[1][1] == 96u, "SYNC's rate: x4 in a new sound");
        {                                       /* a format 21 sound from SYNC's first builds: its rate was its PIT */
            static patch_t o, q;
            o = PATCH_INIT;
            o.osc[0].mode[0] = MODE_SYNC; o.mpitch[0][0] = 12;   /* (x2: 12 semitones) */
            o.osc[1].mode[1] = MODE_MIX; o.mpitch[1][1] = 30;
            CHECK(patch_from(&q, (const uint8_t *)&o, PATCH_V21_SIZE, 21u) == 0 && q.sync_rate[0][0] == 48u &&
                  q.sync_rate[1][1] == 96u && q.mpitch[1][1] == 30u, "format 21: SYNC's rate from its PIT (x2), the "
                  "others x4");
        }
    }

    /* ---- the delay's FDBK at the top: it self-oscillates, held (no hard clip); lower down, the repeats die away */
    {
        static const uint8_t FB[3] = {100, 112, 127};
        uint64_t late[3];
        int32_t peak = 0;
        uint32_t f, j, q;
        for (f = 0; f < 3u; f++) {
            fresh();
            edit.dly_mix = 100; edit.dly_time = 40; edit.dly_fb = FB[f]; edit.dly_ping = 0;
            engine_set(&edit);
            engine_note_on(60); run0(2000); engine_note_off(60);
            run0(44100u * 6u);                /* six seconds after the note */
            late[f] = 0;
            for (q = 0; q < 44100u; q += BLK) {
                memset(buf, 0, sizeof buf);
                engine_render(buf, BLK);
                for (j = 0; j < 2u * BLK; j++) {
                    int32_t a = buf[j] < 0 ? -buf[j] : buf[j];
                    late[f] += (uint64_t)((int64_t)buf[j] * buf[j] >> 16);
                    if (f == 2u && a > peak) peak = a;
                }
            }
        }
        CHECK(dly_fb_of(112) == 112 * 31000 / 127 && dly_fb_of(127) > 32768,
              "FDBK: as before up to 112, past unity at the top (%d)", (int)dly_fb_of(127));
        CHECK(late[0] < 1000u && late[1] < late[2] / 100u,
              "FDBK 100, 112: gone after six seconds (%llu, %llu)", (unsigned long long)late[0], (unsigned long long)late[1]);
        CHECK(late[2] > 100000u && peak < 30000,
              "FDBK 127: still sounding after six seconds, self-oscillating (%llu), held under full scale (peak %d)",
              (unsigned long long)late[2], (int)peak);
    }

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
    loop_note(60, 100, 2);
    engine_note_on(60);
    run(4000, 0);
    CHECK(lp.state == LP_REC && lp.run, "... a note: recording");
    loop_note(60, 0, 2);
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
