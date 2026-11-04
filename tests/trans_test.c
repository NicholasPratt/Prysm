/* SPDX-License-Identifier: GPL-3.0-only */
/* The attack transients (patch format 17), on a computer, through the app's own code: a format 16 sound reads with
 * them off; one sounds through a slow amp attack (its own envelope, past the amp); a pitched one follows the key (an
 * octave up: half as long), an unpitched one does not; TUNE moves either; DECAY fades it sooner; a softer note is
 * quieter; the split's other side and a LIVE mute silence it; each of the 24 plays, ends, and stays in range.
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t buf[2u * BLK];
static int32_t peak_abs;
static uint64_t run(uint32_t n)                 /* n frames; their energy */
{
    uint64_t e = 0;
    uint32_t i;
    for (; n >= BLK; n -= BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
        for (i = 0; i < 2u * BLK; i++) {
            e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
            if (buf[i] > peak_abs) peak_abs = buf[i];
            if (-buf[i] > peak_abs) peak_abs = -buf[i];
        }
    }
    return e;
}

/* the transient alone: the oscillators silent (levels 0), the amp held open (or slow), no effects */
static patch_t base(uint32_t type, uint32_t o)
{
    patch_t p = PATCH_INIT;
    p.lvl1 = p.lvl2 = 0;
    p.noise_lvl = 0;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    p.amp_a = 0; p.amp_s = 127;
    p.trn_type[o] = (uint8_t)type;
    p.trn_lvl[o] = 127;
    return p;
}
static void quiet(void);
static void tplay(const patch_t *p, uint32_t note, uint32_t vel)
{
    quiet();
    edit = *p;
    engine_init();
    engine_set(&edit);
    engine_note_on_vel(note, vel);
}
static int any_trn(void)                       /* a transient still playing, in any voice */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].t_g[0] || voice[v].t_g[1]) return 1;
    return 0;
}
static void quiet(void)                         /* every voice and transient done (engine_init leaves the voices) */
{
    uint32_t v;
    engine_all_off();
    for (v = 0; v < NV; v++) voice[v].t_g[0] = voice[v].t_g[1] = 0;
    run(44100);
}
static uint32_t length(const patch_t *p, uint32_t note)   /* frames until the transient is done (its level 0) */
{
    uint32_t n = 0;
    quiet();
    tplay(p, note, 127);
    run(BLK);
    n = BLK;
    while (n < 44100u && any_trn()) { run(BLK); n += BLK; }
    return n;
}

int main(void)
{
    patch_t p, q;
    uint32_t t, a, b, bad = 0, silent = 0;
    uint64_t e1, e2;
    uint8_t old[PATCH_V16_SIZE];
    printf("TRANSIENTS\n");
    p = PATCH_INIT;
    p.cut = 33;
    memcpy(old, &p, sizeof old);
    memset(&q, 0xAA, sizeof q);
    CHECK(patch_from(&q, old, PATCH_V16_SIZE, 16) == 0 && q.cut == 33 && q.trn_type[0] == 0 && q.trn_type[1] == 0 &&
          q.trn_dec[0] == 127, "a format 16 sound reads, its transients off");

    p = base(4, 0);                             /* MALLET on osc 1, the amp's attack 5 s: the transient heard at once */
    p.lvl1 = 110;
    p.amp_a = 120;
    tplay(&p, 60, 127);
    e1 = run(4410);
    q = p; q.trn_type[0] = 0;
    tplay(&q, 60, 127);
    e2 = run(4410);
    CHECK(e1 > 50u * (e2 + 1u), "through a slow amp attack: heard in the first 100 ms (%llu vs %llu without)",
          (unsigned long long)e1, (unsigned long long)e2);

    p = base(4, 0);                             /* pitched: an octave up, half as long */
    a = length(&p, 60); b = length(&p, 72);
    CHECK(b * 2u > a - 64u && b * 2u < a + 64u, "MALLET (pitched): C5 lasts half C4's (%u, %u frames)", a, b);
    p.trn_tune[0] = 36;                         /* TUNE +12 */
    b = length(&p, 60);
    CHECK(b * 2u > a - 64u && b * 2u < a + 64u, "TUNE +12 at C4: as C5 (%u frames)", b);
    p = base(16, 0);                            /* KNOCK: unpitched */
    a = length(&p, 48); b = length(&p, 84);
    CHECK(a == b, "KNOCK (unpitched): as long on any note (%u, %u frames)", a, b);

    p = base(6, 0);                             /* BELL: DECAY down, less of its later part */
    tplay(&p, 60, 127); run(2205); e1 = run(8820);
    p.trn_dec[0] = 60;
    tplay(&p, 60, 127); run(2205); e2 = run(8820);
    CHECK(e2 * 4u < e1, "DECAY 60: the bell's tail from 50 ms faded (%llu vs %llu at FULL)",
          (unsigned long long)e2, (unsigned long long)e1);

    p = base(1, 1);                             /* velocity, with AMP velocity on */
    p.vel_amp = 127;
    tplay(&p, 60, 127); e1 = run(4410);
    tplay(&p, 60, 40); e2 = run(4410);
    CHECK(e2 * 4u < e1, "a soft note's transient is quieter (%llu vs %llu)", (unsigned long long)e2, (unsigned long long)e1);

    p = base(1, 1);                             /* the split: osc 2's transient only from the split up */
    p.split = 60;
    tplay(&p, 48, 127); e1 = run(4410);
    tplay(&p, 72, 127); e2 = run(4410);
    CHECK(e1 == 0 && e2 > 0, "split: osc 2's transient silent below it, heard above (%llu, %llu)",
          (unsigned long long)e1, (unsigned long long)e2);
    p = base(1, 0);
    live_mute = 1u << LM_OSC1;
    tplay(&p, 60, 127); e1 = run(4410);
    live_mute = 0;
    CHECK(e1 == 0, "a LIVE mute on OSC 1 silences its transient");

    p = base(6, 0);                             /* TYPE changed while one plays: to OFF (silent), to a shorter one */
    tplay(&p, 60, 127);
    run(2205);
    edit.trn_type[0] = 0;
    engine_set(&edit);
    e1 = run(2205);
    edit.trn_type[0] = 6;
    engine_set(&edit);
    quiet();
    tplay(&p, 60, 127);
    run(4410);                                  /* (100 ms into the 300 ms bell) */
    edit.trn_type[0] = 17;                      /* CLICK: 20 ms long */
    engine_set(&edit);
    run(BLK);
    CHECK(e1 == 0 && !any_trn(), "TYPE changed midway: OFF silent at once; a shorter one, past its end, done");

    for (t = 1; t <= TR_N; t++) {               /* each: heard, done in its length, never past full scale */
        uint32_t n;
        p = base(t, t & 1u);
        p.trn_tune[t & 1u] = 0;                 /* (2 octaves down on the lowest note: its longest) */
        peak_abs = 0;
        tplay(&p, 24, 127);
        e1 = run(4410);
        n = 4410;
        while (n < 44100u * 6u && any_trn()) { run(BLK); n += BLK; }
        if (!e1) silent++;
        if (n >= 44100u * 6u || peak_abs > 32767) { bad++; printf("    %s: %u frames, peak %d\n", TR_NAME[t], n, (int)peak_abs); }
        p.trn_tune[t & 1u] = 48;                /* (2 octaves up on the top note: its fastest) */
        tplay(&p, 120, 127);
        run(44100);
        if (any_trn()) { bad++; printf("    %s: still playing up high\n", TR_NAME[t]); }
    }
    CHECK(silent == 0 && bad == 0, "all %u: heard, ended, in range, at any pitch (%u silent, %u bad)", TR_N, silent, bad);
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
