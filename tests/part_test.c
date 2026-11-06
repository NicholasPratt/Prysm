/* SPDX-License-Identifier: GPL-3.0-only */
/* The two parts (patch formats 18 .. 20), on a computer, through the app's own code: a format 17 sound reads as SINGLE with
 * part B as its part A; PATCH_INIT's part B is its part A; DUAL plays the part the keys are on, its oscillator alone
 * through its own amp; SPLIT picks the part by the note; LAYER plays every note on two voices, A and B, the arp's too,
 * and lets both go; part B's own LFO and filter are its own; a format 18 sound keeps its parts, its shared modulators on
 * both; LFO 2, M.ENV 2 and a matrix route on one part move that part's voices alone (on both in SINGLE); the
 * sequencer's lanes play their own parts (MUTE2 rests B's); a version 5 sequencer reads; the looper keeps a note's part.
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
static void two_lanes(void)                     /* pattern 1's first step: C4 on part B (lane 0), E4 on part A (lane 1) */
{
    seqs.pat[0].note[0] = 60; seqs.pat[0].trig[0] = SEQ_NOTE;
    seqs.pat[0].xnote[0][0] = 64; seqs.pat[0].xvel[0][0] = 100;
    seqs.pat[0].lpart[0] = 1u;
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

    {                                           /* the sequencer: each lane's note on its part (DUAL) */
        uint32_t v, a_ok = 0, b_ok = 0, n;
        p = PATCH_INIT;
        p.vmode = VM_DUAL;
        start(&p);
        seqs_init();
        two_lanes();
        key_part = 0;
        engine_seq_play(1);
        for (n = 0; n < 40u && !(a_ok && b_ok); n++) {
            run(BLK);
            for (v = 0; v < NV; v++)
                if (voice[v].gate) {
                    if (voice[v].note == 60 && voice[v].part == 1u) b_ok = 1;
                    if (voice[v].note == 64 && voice[v].part == 0u) a_ok = 1;
                }
        }
        engine_seq_play(0);
        CHECK(a_ok && b_ok, "DUAL: the sequencer plays each lane's note on its part (keys on A)");
        start(&p);
        two_lanes();
        seqs.part_mute[1] = 1;
        engine_seq_play(1);
        for (n = 0, b_ok = a_ok = 0; n < 40u; n++) {
            run(BLK);
            for (v = 0; v < NV; v++) if (voice[v].gate) { b_ok |= voice[v].note == 60; a_ok |= voice[v].note == 64; }
        }
        engine_seq_play(0);
        seqs.part_mute[1] = 0;
        CHECK(a_ok && !b_ok, "  MUTE2: part B's notes rest, A's play");
    }
    {                                           /* step entry: a key's note on the part edited; a MIDI channel's on its */
        p = PATCH_INIT;
        p.vmode = VM_DUAL;
        start(&p);
        seqs_init();
        seq_rec = 1; page = PG_SEQ; seq_cursor = 3; ui_part = 1;
        ui_seq_key(60, 100, 2);                 /* (the keys: part B, edited) */
        ui_seq_key(64, 100, 0);                 /* (IN's channel: part A) */
        CHECK(seqs.pat[0].note[3] == 60 && (seqs.pat[0].lpart[3] & 3u) == 1u,
              "step entry: a key's note on the part edited (B), IN's on A");
        seq_rec = 0; ui_part = 0; page = 0;
        memset(entry_down, 0, sizeof entry_down); entry_n = 0;
    }
    {                                           /* a version 5 sequencer: its notes, every lane on part A */
        static uint8_t old5[SEQS_V5_SIZE];
        static seqs_t got;
        uint32_t i;
        two_lanes();
        memcpy(old5, &seqs, 4u);
        for (i = 0; i < SEQ_PATS; i++) {
            memset(&seqs.pat[i].lpart, 0xFF, sizeof seqs.pat[i].lpart);
            memcpy(old5 + 4u + i * SEQS_V5_PAT, &seqs.pat[i], SEQS_V5_PAT);
        }
        memcpy(old5 + 4u + SEQ_PATS * SEQS_V5_PAT, (uint8_t *)&seqs + __builtin_offsetof(seqs_t, div),
               sizeof(seqs_t) - __builtin_offsetof(seqs_t, div));
        seqs.song_len = 7;
        memcpy(old5 + 4u + SEQ_PATS * SEQS_V5_PAT + (__builtin_offsetof(seqs_t, song_len) - __builtin_offsetof(seqs_t, div)),
               &seqs.song_len, 1);
        CHECK(seqs_size(5) == SEQS_V5_SIZE && seqs_conv(&got, old5, 5) == 0 && got.pat[0].note[0] == 60 &&
              got.pat[0].xnote[0][0] == 64 && got.pat[0].lpart[0] == 0 && got.song_len == 7 &&
              got.version == SEQS_VERSION, "a version 5 sequencer reads: its notes, its song, every lane on part A");
    }
    {                                           /* the looper: a note recorded on part B plays back on B */
        uint32_t v, b_ok = 0, n;
        p = PATCH_INIT;
        p.vmode = VM_DUAL;
        start(&p);
        lp_bars = 1;
        loop_cmd(LPC_REC);
        run(BLK);
        loop_note(62, 100, 1);
        engine_note_on_part(62, 100, 1);
        run(4000);
        loop_note(62, 0, 1);
        engine_note_off_part(62, 1);
        run(44100 * 3);
        loop_cmd(LPC_RECOFF);
        engine_all_off();
        for (n = 0; n < 44100u * 3u / BLK && !b_ok; n++) {
            run(BLK);
            for (v = 0; v < NV; v++) b_ok |= voice[v].gate && voice[v].note == 62 && voice[v].part == 1u;
        }
        loop_cmd(LPC_CLEAR);
        run(BLK * 4u);
        CHECK(b_ok && lp.ev[0].vel == (100u | 128u), "the looper: a note played on part B plays back on B");
    }

    /* the parts' levels and places (format 20): as they are, nothing changes; B hard right: its left silent, its
     * right as before; A's level 0: A silent; SINGLE: neither; a long render: as two of the bus's length */
    {
        static int32_t l0[2u * 4410u], l1[2u * 4410u];
        uint64_t el, er, el0, er0;
        uint32_t f;
        p = PATCH_INIT;
        p.cho_mix = p.dly_mix = p.rev_mix = 0;
        p.vmode = VM_DUAL;
#define TAKE(buf_, part_) do { start(&p); key_part = (uint8_t)(part_); engine_note_on_vel(60, 100); key_part = 0; \
            for (f = 0; f < 4410u; f += BLK) { memset(buf, 0, sizeof buf); engine_render(buf, BLK); \
                memcpy((buf_) + 2u * f, buf, sizeof buf); } } while (0)
        TAKE(l0, 1);
        CHECK(!eg.pbus, "DUAL, levels and places as they are: no bus");
        p.ppan[1] = 127;
        TAKE(l1, 1);
        for (f = 0, el = er = el0 = er0 = 0; f < 4410u; f++) {
            el += (uint64_t)((int64_t)l1[2u * f] * l1[2u * f] >> 16); er += (uint64_t)((int64_t)l1[2u * f + 1u] * l1[2u * f + 1u] >> 16);
            el0 += (uint64_t)((int64_t)l0[2u * f] * l0[2u * f] >> 16); er0 += (uint64_t)((int64_t)l0[2u * f + 1u] * l0[2u * f + 1u] >> 16);
        }
        CHECK(eg.pbus && el == 0 && er0 > 0 && er > er0 - er0 / 100u && er < er0 + er0 / 100u,
              "B hard right: its left silent, its right as it was (within 1 %%: the free LFO)");
        TAKE(l1, 0);
        for (f = 0, ok = 1; f < 4410u; f++) ok &= l1[2u * f] == l1[2u * f + 1u];
        CHECK(ok, "A, centred: left and right the same with B moved");
        p.ppan[1] = 64; p.pvol[0] = 0;
        TAKE(l1, 0);
        for (f = 0, ok = 1; f < 4410u; f++) ok &= l1[2u * f] == 0;
        CHECK(ok, "A's level 0: A silent");
        p.vmode = VM_SINGLE;
        TAKE(l1, 0);
        CHECK(!eg.pbus && run(BLK * 8u) > 0, "SINGLE: the levels and places not used");
        p.vmode = VM_DUAL; p.pvol[0] = 127; p.ppan[1] = 127;
        start(&p);
        key_part = 1;
        engine_note_on_vel(60, 100);
        key_part = 0;
        memset(l0, 0, sizeof l0);
        engine_render(l0, 512u);               /* (past the bus's 256: two renders) */
        for (f = 0, el = 0, er = 0, ok = 0; f < 512u; f++) {
            el |= (uint32_t)l0[2u * f];
            if (f < 256u) er |= l0[2u * f + 1u] != 0; else ok |= l0[2u * f + 1u] != 0;
        }
        CHECK(!el && er && ok, "a render of 512 frames: both halves through the bus, B still right");
#undef TAKE
        {
            static uint8_t o19[PATCH_V19_SIZE];
            p = PATCH_INIT;
            p.vmode = VM_LAYER;
            memcpy(o19, &p, sizeof o19);
            memset(&q, 0xAA, sizeof q);
            CHECK(patch_from(&q, o19, PATCH_V19_SIZE, 19) == 0 && q.vmode == VM_LAYER && q.pvol[0] == 127 &&
                  q.pvol[1] == 127 && q.ppan[0] == 64 && q.ppan[1] == 64, "a format 19 sound: levels full, centred");
        }
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
