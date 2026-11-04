/* SPDX-License-Identifier: GPL-3.0-only */
/* The two parts (patch format 18), on a computer, through the app's own code: a format 17 sound reads as SINGLE with
 * part B as its part A; PATCH_INIT's part B is its part A; DUAL plays the part the keys are on, its oscillator alone
 * through its own amp; SPLIT picks the part by the note; LAYER plays every note on two voices, A and B, the arp's too,
 * and lets both go; part B's own LFO and filter are its own; a format 18 sound keeps its parts, its shared modulators on
 * both; LFO 2, M.ENV 2 and a matrix route on one part move that part's voices alone (on both in SINGLE).
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t buf[2u * BLK];
static uint64_t run(uint32_t n)                 /* n frames; their energy */
{
    uint64_t e = 0;
    uint32_t i;
    for (; n >= BLK; n -= BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
        for (i = 0; i < 2u * BLK; i++) e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
    }
    return e;
}
static void setb(patch_t *p, uint32_t off, uint8_t val)   /* part B's own of the field at off */
{
    uint32_t i;
    for (i = 0; i < PART_N; i++)
        if (PART_OFF[i] == off) { p->pb[i] = val; return; }
    printf("    (no part field at %u)\n", off);
    fails++;
}
static void start(const patch_t *p)
{
    uint32_t v;
    engine_all_off();
    for (v = 0; v < NV; v++) voice[v].t_g[0] = voice[v].t_g[1] = 0;
    run(44100 * 2);
    edit = *p;
    engine_init();
    engine_set(&edit);
}
static uint64_t heard(const patch_t *p, uint32_t part)   /* a note on that part (DUAL), its first 200 ms: a hash */
{
    uint64_t h = 1469598103934665603ull;
    uint32_t n, i;
    start(p);
    key_part = (uint8_t)part;
    engine_note_on_vel(60, 100);
    for (n = 0; n < 8820u; n += BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
        for (i = 0; i < 2u * BLK; i++) { h ^= (uint32_t)buf[i]; h *= 1099511628211ull; }
    }
    key_part = 0;
    return h;
}
static uint32_t sounding(uint32_t part)         /* voices gated on that part */
{
    uint32_t v, n = 0;
    for (v = 0; v < NV; v++) n += voice[v].gate && voice[v].part == part;
    return n;
}
static uint32_t on(void)
{
    uint32_t v, n = 0;
    for (v = 0; v < NV; v++) n += voice_on(v);
    return n;
}

int main(void)
{
    patch_t p, q;
    uint8_t old[PATCH_V17_SIZE];
    uint32_t i, ok;
    uint64_t ea, eb2;
    printf("PARTS\n");
    p = PATCH_INIT;
    ok = 1;
    for (i = 0; i < PART_N; i++) ok &= p.pb[i] == ((uint8_t *)&p)[PART_OFF[i]];
    CHECK(ok && p.vmode == VM_SINGLE, "the default sound: SINGLE, part B as part A");
    for (i = 0; i < PART_N; i++)                /* (no field twice) */
        for (ok = i + 1u; ok < PART_N; ok++)
            if (PART_OFF[i] == PART_OFF[ok]) { printf("    field %u twice\n", PART_OFF[i]); fails++; }
    p.cut = 33; p.amp_r = 99; p.noise_col = 7;
    memcpy(old, &p, sizeof old);
    memset(&q, 0xAA, sizeof q);
    CHECK(patch_from(&q, old, PATCH_V17_SIZE, 17) == 0 && q.vmode == VM_SINGLE && q.pb[0] == 33 &&
          q.pb[PB_NOISE_COL] == 7 && q.cut == 33, "a format 17 sound reads as SINGLE, part B as its A");

    /* DUAL: A's amp quick, B's slow (5 s): the keys on A heard at once, on B not yet; each its oscillator alone */
    p = PATCH_INIT;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    p.vmode = VM_DUAL;
    p.amp_a = 0;
    setb(&p, __builtin_offsetof(patch_t, amp_a), 120);
    start(&p);
    key_part = 0;
    engine_note_on_vel(60, 127);
    ea = run(4410);
    CHECK(sounding(0) == 1 && sounding(1) == 0 && voice[0].omask == 1u, "DUAL on A: a voice of part A, osc 1 alone");
    start(&p);
    key_part = 1;
    engine_note_on_vel(60, 127);
    eb2 = run(4410);
    CHECK(sounding(1) == 1 && sounding(0) == 0, "DUAL on B: a voice of part B");
    {
        uint32_t v;
        for (v = 0; v < NV && !voice[v].gate; v++) {}
        CHECK(v < NV && voice[v].omask == 2u, "  its oscillator: osc 2 alone");
    }
    CHECK(ea > 20u * (eb2 + 1u), "  each its own amp: B's slow attack quiet at first (%llu vs A's %llu)",
          (unsigned long long)eb2, (unsigned long long)ea);
    key_part = 0;
    engine_note_on_vel(64, 127);                /* (B's still held: the voices shared) */
    run(BLK);
    CHECK(sounding(0) == 1 && sounding(1) == 1, "  both parts at once, the voices shared");
    engine_note_off(60); engine_note_off(64);
    run(44100 * 12);
    CHECK(on() == 0, "  both let go");

    /* SPLIT */
    p.vmode = VM_SPLIT;
    p.split = 60;
    start(&p);
    engine_note_on_vel(48, 127);
    engine_note_on_vel(72, 127);
    run(BLK);
    ok = 1;
    for (i = 0; i < NV; i++)
        if (voice[i].gate) ok &= voice[i].part == (voice[i].note >= 60) && voice[i].omask == 1u << voice[i].part;
    CHECK(ok && sounding(0) == 1 && sounding(1) == 1, "SPLIT: below it part A (osc 1), from it part B (osc 2)");

    /* LAYER: two voices a note, A and B */
    p = PATCH_INIT;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    p.vmode = VM_LAYER;
    start(&p);
    engine_note_on_vel(60, 127);
    run(BLK);
    CHECK(sounding(0) == 1 && sounding(1) == 1 && voice[0].note == voice[1].note && voice[1].omask == 2u,
          "LAYER: a note on two voices, A (osc 1) and B (osc 2)");
    engine_note_on_vel(64, 127);
    engine_note_on_vel(67, 127);                /* (a third: the oldest pair goes) */
    run(BLK);
    CHECK(sounding(0) == 2 && sounding(1) == 2, "  two notes at once (%u, %u)", sounding(0), sounding(1));
    engine_note_off(60); engine_note_off(64); engine_note_off(67);
    run(44100 * 12);
    CHECK(on() == 0, "  all let go");
    p.arp_on = 1;                               /* the arp's notes: on both */
    start(&p);
    held.note[0] = 60; held.vel[0] = 127; held.n = 1;
    run(44100 / 2);
    ok = 0;
    for (i = 0; i < 2000u && !ok; i++) { run(BLK); ok = sounding(0) == 1 && sounding(1) == 1; }
    CHECK(ok, "  the arp's notes on both parts");
    held.n = 0;

    /* SINGLE ignores part B; DUAL hears it: B's filter shut is B's alone */
    p = PATCH_INIT;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    p.lvl1 = p.lvl2 = 100;
    setb(&p, __builtin_offsetof(patch_t, cut), 0);
    setb(&p, __builtin_offsetof(patch_t, fenv), 64);
    start(&p);
    engine_note_on_vel(60, 127);
    ea = run(8820);
    q = p; for (i = 0; i < PART_N; i++) q.pb[i] = ((uint8_t *)&q)[PART_OFF[i]];
    start(&q);
    engine_note_on_vel(60, 127);
    CHECK(run(8820) == ea, "SINGLE: part B's own fields unheard");
    p.vmode = VM_DUAL;
    start(&p);
    key_part = 1;
    engine_note_on_vel(60, 127);
    eb2 = run(8820);
    start(&p);
    key_part = 0;
    engine_note_on_vel(60, 127);
    ea = run(8820);
    CHECK(eb2 * 8u < ea, "DUAL: B's filter shut darkens B alone (%llu vs A's %llu)", (unsigned long long)eb2,
          (unsigned long long)ea);
    key_part = 0;

    p = PATCH_INIT;                             /* format 18: its parts kept, the shared modulators on both */
    p.vmode = VM_LAYER; p.pb[0] = 11;
    memcpy(old, &p, sizeof old);
    {
        uint8_t o18[PATCH_V18_SIZE];
        memcpy(o18, &p, sizeof o18);
        memset(&q, 0xAA, sizeof q);
        CHECK(patch_from(&q, o18, PATCH_V18_SIZE, 18) == 0 && q.vmode == VM_LAYER && q.pb[0] == 11 &&
              q.l2_part == PT_BOTH && q.me2_part == PT_BOTH && q.mm_part[3] == PT_BOTH,
              "a format 18 sound keeps its parts, its shared modulators on both");
    }
    {                                           /* each shared modulator on one part: that part's sound alone moves */
        static const char *const what[3] = {"LFO 2", "M.ENV 2", "a matrix route"};
        uint32_t w, t;
        for (w = 0; w < 3u; w++)
            for (t = PT_A; t <= PT_B; t++) {
                patch_t off = PATCH_INIT, on;
                uint64_t a0, a1, b0, b1;
                off.cho_mix = off.dly_mix = off.rev_mix = 0;
                off.vmode = VM_DUAL;
                on = off;
                if (w == 0) { on.lfo2_dest = DEST_AMP; on.lfo2_amt = 127; on.lfo2_rate = 110; on.l2_part = (uint8_t)t; }
                if (w == 1) { on.menv2_dest = DEST_CUTOFF; on.menv2_amt = 0; on.menv2_d = 40; on.me2_part = (uint8_t)t; }
                if (w == 2) { on.mm_src[0] = MM_VEL; on.mm_dest[0] = DEST_PITCH; on.mm_amt[0] = 127; on.mm_part[0] = (uint8_t)t; }
                a0 = heard(&off, 0); a1 = heard(&on, 0); b0 = heard(&off, 1); b1 = heard(&on, 1);
                CHECK(t == PT_A ? a0 != a1 && b0 == b1 : a0 == a1 && b0 != b1, "%s on part %s: %s moves, %s as without it",
                      what[w], t == PT_A ? "A" : "B", t == PT_A ? "A" : "B", t == PT_A ? "B" : "A");
            }
        p = PATCH_INIT;                         /* SINGLE: on every voice, whatever its PART says */
        p.cho_mix = p.dly_mix = p.rev_mix = 0;
        p.lfo2_dest = DEST_AMP; p.lfo2_amt = 127; p.lfo2_rate = 110;
        q = p; q.l2_part = PT_B;
        CHECK(heard(&p, 0) == heard(&q, 0), "SINGLE: LFO 2 on part B still on every voice");
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
