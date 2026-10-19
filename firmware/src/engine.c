/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's sound engine: 4 voices, each 2 oscillators, each a wavetable carrier modulated by a series chain of two
 * wavetable modulators (mod 2 -> mod 1 -> carrier), each modulator with its own AR envelope and a mode (FM, RING,
 * MIX); the two oscillators mixed, driven, through a 2-pole low-pass with its own ADSR, then the amp ADSR.
 *
 * Who runs what:
 *   main loop   engine_set(): the patch changed -> the derived values below; engine_note_on / _off.
 *               engine_tables(): a bank or position changed -> that operator's wave, morphed, into RAM.
 *   audio ISR   engine_render(): reads only RAM and the derived values; one writer each (32-bit stores).
 * Integers only (no FPU): levels Q15, envelopes Q24, phases 32 bit. Every float is in tools/gen_prysm_tables.py.
 * The file stands alone (stdint, the tables) so tests/engine_test.c runs it on a computer. */
#include "prysm_tables.h"

#define NV 4u                                   /* voices */
#define NOSC 2u                                 /* oscillators per voice */
#define NOP 3u                                  /* operators per oscillator: 0 carrier, 1 mod 1, 2 mod 2 */
#define BLK 16u                                 /* frames per envelope step (gen_prysm_tables.py BLK) */
#define BLK_SHIFT 4                             /* log2(BLK): a block's glide step is a shift */
#define ENV_FULL (1 << 24)
#define VOICE_GAIN 8000                         /* Q15: four voices at full level cannot clip (4 x 8000 < 32767) */

enum { MODE_FM, MODE_RING, MODE_MIX, MODE_N };
static const char *const MODE_NAME[MODE_N] = {"FM", "RING", "MIX"};

/* ---- the patch: what the pages edit (all small integers, 0..127 unless said) */
typedef struct {
    struct {
        uint8_t bank[NOP], pos[NOP];            /* bank 0..PW_BANKS-1, position 0..127 across its 8 waves */
        uint8_t ratio[NOP];                     /* 0..PR_RATIOS-1 (PR_RATIO_NAME) */
        uint8_t fine;                           /* 64 = in tune, +-50 cents */
        uint8_t mode[2];                        /* [0] how mod 1 acts on the carrier, [1] how mod 2 acts on mod 1 */
        uint8_t att[2], rel[2];                 /* mod 1, mod 2 AR: 1 ms .. 10 s */
        uint8_t amt[2];                         /* mod 1, mod 2 amount */
        uint8_t level;                          /* the oscillator's level */
    } osc[NOSC];
    uint8_t cut, res;                           /* filter cutoff (8 Hz .. 20 kHz), resonance */
    uint8_t fenv;                               /* filter envelope amount: 64 = none, +-60 semitones */
    uint8_t key;                                /* keytrack: 0 .. 127 = 0 .. 100 % */
    uint8_t drive;                              /* into the filter: 0 = clean (bypassed), gain up to 8 */
    uint8_t flt_a, flt_d, flt_s, flt_r;         /* filter ADSR */
    uint8_t amp_a, amp_d, amp_s, amp_r;         /* amp ADSR */
    uint8_t lvl1, lvl2;                         /* MIX page: the oscillators' levels */
    uint8_t semi2;                              /* osc 2: 0..48, 24 = in tune */
    uint8_t fine2;                              /* osc 2: 64 = in tune */
} patch_t;

static const patch_t PATCH_INIT = {
    .osc = {
        {.bank = {0, 0, 0}, .pos = {0, 0, 0}, .ratio = {1, 3, 1}, .fine = 64, .mode = {MODE_FM, MODE_FM},
         .att = {0, 0}, .rel = {60, 60}, .amt = {70, 0}, .level = 127},
        {.bank = {0, 0, 0}, .pos = {73, 0, 0}, .ratio = {1, 1, 1}, .fine = 64, .mode = {MODE_FM, MODE_FM},
         .att = {0, 0}, .rel = {60, 60}, .amt = {0, 0}, .level = 127},
    },
    .cut = 92, .res = 40, .fenv = 88, .key = 64, .drive = 0,
    .flt_a = 0, .flt_d = 75, .flt_s = 50, .flt_r = 65,
    .amp_a = 8, .amp_d = 70, .amp_s = 100, .amp_r = 60,
    .lvl1 = 110, .lvl2 = 70, .semi2 = 24, .fine2 = 70,
};

/* ---- derived from the patch by engine_set(), read by the ISR */
static volatile struct {
    uint32_t m_att[NOSC][2], m_rel[NOSC][2];    /* mod envelopes: attack step (Q24), release coefficient (Q16) */
    int32_t m_amt[NOSC][2];                     /* mod amounts, Q15 */
    uint8_t mode[NOSC][2];
    int32_t lvl[NOSC];                          /* oscillator output levels, Q15 */
    uint32_t a_att, a_dec, a_rel;               /* amp ADSR */
    int32_t a_sus;                              /* Q24 */
    uint32_t f_att, f_dec, f_rel;               /* filter ADSR */
    int32_t f_sus;
    int32_t f_cut;                              /* cutoff, quarter semitones (PR_FILT_G index) */
    int32_t f_env;                              /* envelope depth, quarter semitones at full envelope (+-) */
    int32_t f_key;                              /* keytrack, Q8 (256 = 100 %: a semitone per semitone) */
    uint32_t f_k;                               /* damping, Q24 */
    int32_t drive;                              /* gain Q12; 0 = no drive stage */
} eg;

/* ---- the operators' waves: the morph of two neighbours, in RAM, double-buffered (engine_tables writes the one
 * the ISR is not reading, then swaps the pointer: a 32-bit store) */
static int16_t op_wave[NOSC][NOP][2][PW_N + 1];   /* + 1: a copy of [0], so the interpolation never wraps */
static const int16_t *volatile op_cur[NOSC][NOP];

/* ---- voices */
typedef struct { int32_t lvl; uint8_t st; } env_t;   /* st: 0 idle, 1 attack, 2 decay / hold, 3 release */
static struct {
    volatile uint32_t inc[NOSC][NOP];           /* phase steps (main loop) */
    volatile uint8_t gate;                      /* the key is down (main loop) */
    volatile uint8_t trig;                      /* a new note: the ISR restarts the envelopes (main sets, ISR clears) */
    uint32_t ph[NOSC][NOP];
    env_t amp, flt, mod[NOSC][2];
    int32_t ic1, ic2;                           /* the filter's two integrator states */
    int32_t f_cs;                               /* the cutoff knob, smoothed (quarter semitones, Q8) */
    int32_t f_ks;                               /* the damping, smoothed (Q24) */
    int32_t fc[3];                              /* the coefficients the last block ended on (Q28) */
    int32_t amp_q15, mod_q15[NOSC][2];          /* the levels now (per frame, gliding to each block's target) */
    uint8_t note;
    uint32_t age;
} voice[NV];

_Static_assert(BLK == 1u << BLK_SHIFT, "BLK is a power of two");
static patch_t patch;
static uint32_t ages;

/* ---- envelopes (once per block) */
static int32_t env_step(env_t *e, uint32_t gate, uint32_t att, uint32_t dec, int32_t sus, uint32_t rel)
{
    int64_t d;
    if (!gate && (e->st == 1u || e->st == 2u))
        e->st = 3;
    switch (e->st) {
    case 1:                                     /* attack: linear, from where it is */
        e->lvl += (int32_t)att;
        if (e->lvl >= ENV_FULL) { e->lvl = ENV_FULL; e->st = 2; }
        break;
    case 2:                                     /* decay to the sustain level (hold, for an AR: sus = full) */
        e->lvl -= (int32_t)(((int64_t)(e->lvl - sus) * dec) >> 16);
        break;
    case 3:                                     /* release: exponential, then off */
        d = ((int64_t)e->lvl * rel) >> 16;
        e->lvl -= d > 0 ? (int32_t)d : 1;
        if (e->lvl <= 256) { e->lvl = 0; e->st = 0; }
        break;
    default:
        e->lvl = 0;
    }
    return e->lvl;
}

/* ---- the operators (per frame) */
static inline int32_t wave_at(const int16_t *t, uint32_t ph)   /* -> Q15, linearly interpolated */
{
    uint32_t i = ph >> 24;
    int32_t a = t[i], f = (int32_t)(ph >> 9 & 0x7FFFu);
    return a + (((t[i + 1u] - a) * f) >> 15);
}

/* the wave t at phase ph under a modulator whose output is y (already scaled by its level e) */
static inline int32_t modulated(uint32_t mode, const int16_t *t, uint32_t ph, int32_t y, int32_t e)
{
    int32_t s;
    if (mode == MODE_FM)                        /* phase modulation: y = full scale shifts the phase by 2 cycles */
        return wave_at(t, ph + ((uint32_t)y << 18));
    s = wave_at(t, ph);
    if (mode == MODE_RING)                      /* e = 0: s untouched; e = full: s times the modulator */
        return (s * (32767 - e + y)) >> 15;
    s += y;                                     /* MIX: the modulator's wave added */
    return s > 32767 ? 32767 : s < -32767 ? -32767 : s;
}

/* ---- drive: a cubic soft clip, x - 4/27 x^3, flat (at full scale) from 1.5 x full scale on */
static inline int32_t saturate(int32_t x)
{
    int32_t x2, x3;
    if (x > 49151) return 32767;
    if (x < -49151) return -32767;
    x2 = ((x >> 1) * (x >> 1)) >> 13;           /* x^2 (halves first: 49151^2 overflows 32 bits) */
    x3 = (x2 * (x >> 1)) >> 14;                 /* x^3 */
    return x - ((x3 * 4855) >> 15);             /* 4855 = 4/27 in Q15 */
}

/* ---- the filter's coefficients for a voice, once per block: the cutoff (the smoothed knob, the envelope, keytrack;
 * quarter semitones, Q8) -> g, interpolated between the table's quarter semitones, then the zero-delay-feedback
 * SVF's a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 (Q28). The render glides from one block's to the next's. */
#define CUT_MAX ((int32_t)(PR_FILT_N - 1u) << 8)
static void filter_coefs(uint32_t note, int32_t env, int32_t cut_q8, uint32_t k, int32_t *f)
{
    int32_t c = cut_q8 + (int32_t)(((int64_t)eg.f_env * env) >> 16) + ((((int32_t)note - 60) * eg.f_key) << 2);
    uint32_t g, den, i, fr;
    if (c < 0) c = 0;
    if (c > CUT_MAX - 1) c = CUT_MAX - 1;
    i = (uint32_t)c >> 8;
    fr = (uint32_t)c & 255u;
    g = PR_FILT_G[i] + (((PR_FILT_G[i + 1u] - PR_FILT_G[i]) * fr) >> 8);
    den = (uint32_t)(((1u << 24) + (((uint64_t)g * (g + k)) >> 24)) >> 8);   /* (1 + g (g + k)), Q16 */
    f[0] = (int32_t)((0xFFFFFFFFu / den) << 12);                           /* Q16 -> Q28 */
    f[1] = (int32_t)(((int64_t)f[0] * g) >> 24);
    f[2] = (int32_t)(((int64_t)f[1] * g) >> 24);
}

static int32_t glide(int32_t now, int32_t to)   /* one block's step of a ~12 ms glide (32 blocks) */
{
    int32_t d = (to - now) >> 5;
    return d ? now + d : to;
}

/* ---- the audio ISR: add n stereo frames (L R) to out */
static void engine_render(int32_t *out, uint32_t n)
{
    uint32_t v, o, b, i;
    for (v = 0; v < NV; v++) {
        if (voice[v].trig) {                    /* a new note: the envelopes (re)start from where they are */
            voice[v].trig = 0;
            if (!voice[v].amp.st) {             /* from silence: the phases from 0, the same attack every time */
                for (o = 0; o < NOSC; o++)
                    voice[v].ph[o][0] = voice[v].ph[o][1] = voice[v].ph[o][2] = 0;
                voice[v].ic1 = voice[v].ic2 = 0;
                voice[v].f_cs = eg.f_cut << 8;  /* the knob as it is now; the first block starts on its own */
                voice[v].f_ks = (int32_t)eg.f_k;
                voice[v].fc[0] = -1;
            }
            voice[v].amp.st = 1;
            voice[v].flt.st = 1;
            for (o = 0; o < NOSC; o++)
                voice[v].mod[o][0].st = voice[v].mod[o][1].st = 1;
        }
        if (!voice[v].amp.st)
            continue;                           /* silent: costs nothing */
        for (b = 0; b < n; b += BLK) {
            uint32_t gate = voice[v].gate;
            int32_t a_to = env_step(&voice[v].amp, gate, eg.a_att, eg.a_dec, eg.a_sus, eg.a_rel) >> 9;
            int32_t a = voice[v].amp_q15, da = (a_to - a) >> BLK_SHIFT;
            int32_t acc[BLK];
            for (i = 0; i < BLK; i++)
                acc[i] = 0;
            for (o = 0; o < NOSC; o++) {
                const int16_t *t0 = op_cur[o][0], *t1 = op_cur[o][1], *t2 = op_cur[o][2];
                uint32_t p0 = voice[v].ph[o][0], p1 = voice[v].ph[o][1], p2 = voice[v].ph[o][2];
                uint32_t i0 = voice[v].inc[o][0], i1 = voice[v].inc[o][1], i2 = voice[v].inc[o][2];
                uint32_t m1 = eg.mode[o][0], m2 = eg.mode[o][1];
                int32_t lvl = eg.lvl[o];
                int32_t e1_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][0], gate, eg.m_att[o][0], 0, ENV_FULL,
                                                             eg.m_rel[o][0]) * eg.m_amt[o][0]) >> 24);
                int32_t e2_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][1], gate, eg.m_att[o][1], 0, ENV_FULL,
                                                             eg.m_rel[o][1]) * eg.m_amt[o][1]) >> 24);
                int32_t e1 = voice[v].mod_q15[o][0], e2 = voice[v].mod_q15[o][1];
                int32_t d1 = (e1_to - e1) >> BLK_SHIFT, d2 = (e2_to - e2) >> BLK_SHIFT;
                voice[v].mod_q15[o][0] = e1_to;
                voice[v].mod_q15[o][1] = e2_to;
                if (!lvl) {                     /* not heard: keep the phases going, skip the work */
                    voice[v].ph[o][0] = p0 + i0 * BLK;
                    voice[v].ph[o][1] = p1 + i1 * BLK;
                    voice[v].ph[o][2] = p2 + i2 * BLK;
                    continue;
                }
                if (!e2 && !e2_to) {            /* mod 2 silent: two operators */
                    for (i = 0; i < BLK; i++, e1 += d1) {
                        int32_t y1 = (wave_at(t1, p1) * e1) >> 15;
                        acc[i] += (modulated(m1, t0, p0, y1, e1) * lvl) >> 15;
                        p0 += i0; p1 += i1;
                    }
                    p2 += i2 * BLK;
                } else {
                    for (i = 0; i < BLK; i++, e1 += d1, e2 += d2) {
                        int32_t y2 = (wave_at(t2, p2) * e2) >> 15;
                        int32_t y1 = (modulated(m2, t1, p1, y2, e2) * e1) >> 15;
                        acc[i] += (modulated(m1, t0, p0, y1, e1) * lvl) >> 15;
                        p0 += i0; p1 += i1; p2 += i2;
                    }
                }
                voice[v].ph[o][0] = p0;
                voice[v].ph[o][1] = p1;
                voice[v].ph[o][2] = p2;
            }
            {                                   /* drive, the filter, the amp: the voice into both channels */
                int32_t fe = env_step(&voice[v].flt, gate, eg.f_att, eg.f_dec, eg.f_sus, eg.f_rel);
                int32_t f[3], a1, a2, a3, d1, d2, d3, ic1 = voice[v].ic1, ic2 = voice[v].ic2, drive = eg.drive;
                voice[v].f_cs = glide(voice[v].f_cs, eg.f_cut << 8);   /* knob moves glide, they do not step */
                voice[v].f_ks = glide(voice[v].f_ks, (int32_t)eg.f_k);
                filter_coefs(voice[v].note, fe, voice[v].f_cs, (uint32_t)voice[v].f_ks, f);
                if (voice[v].fc[0] < 0) {       /* (a voice from silence: no previous block to glide from) */
                    voice[v].fc[0] = f[0]; voice[v].fc[1] = f[1]; voice[v].fc[2] = f[2];
                }
                a1 = voice[v].fc[0]; a2 = voice[v].fc[1]; a3 = voice[v].fc[2];
                d1 = (f[0] - a1) >> BLK_SHIFT;  /* the coefficients glide across the block, frame by frame */
                d2 = (f[1] - a2) >> BLK_SHIFT;
                d3 = (f[2] - a3) >> BLK_SHIFT;
                for (i = 0; i < BLK; i++, a += da, a1 += d1, a2 += d2, a3 += d3) {
                    int32_t s = acc[i] > 32767 ? 32767 : acc[i] < -32767 ? -32767 : acc[i], v1, v2, v3;
                    if (drive)
                        s = saturate((s * drive) >> 12);
                    v3 = s - ic2;               /* the zero-delay-feedback SVF (Simper): stable at any cutoff */
                    v1 = (int32_t)(((int64_t)a1 * ic1 + (int64_t)a2 * v3) >> 28);
                    v2 = ic2 + (int32_t)(((int64_t)a2 * ic1 + (int64_t)a3 * v3) >> 28);
                    ic1 = 2 * v1 - ic1;
                    ic2 = 2 * v2 - ic2;
                    s = v2 > 32767 ? 32767 : v2 < -32767 ? -32767 : v2;   /* the low-pass */
                    s = (((s * a) >> 15) * VOICE_GAIN) >> 15;
                    out[2u * (b + i)] += s;
                    out[2u * (b + i) + 1u] += s;
                }
                voice[v].ic1 = ic1;
                voice[v].ic2 = ic2;
                voice[v].fc[0] = f[0]; voice[v].fc[1] = f[1]; voice[v].fc[2] = f[2];
            }
            voice[v].amp_q15 = a_to;
        }
    }
}

/* ---- main loop: the patch -> the derived values */
static uint32_t pitch_q16(uint32_t o)           /* the oscillator's tuning: fine (and osc 2's semitones, fine) */
{
    uint32_t f = PR_FINE_Q16[patch.osc[o].fine & 127u];
    if (o == 1u) {
        f = (uint32_t)(((uint64_t)f * PR_SEMI_Q16[patch.semi2 <= 48u ? patch.semi2 : 24u]) >> 16);
        f = (uint32_t)(((uint64_t)f * PR_FINE_Q16[patch.fine2 & 127u]) >> 16);
    }
    return f;
}

static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9): f * 2^32 / 44100; lower octaves shift */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

static void voice_tune(uint32_t v)              /* the operators' phase steps for the voice's note */
{
    uint32_t base = INC_C9[voice[v].note % 12u] >> (10u - voice[v].note / 12u), o, k;
    for (o = 0; o < NOSC; o++) {
        uint64_t f = ((uint64_t)base * pitch_q16(o)) >> 16;
        for (k = 0; k < NOP; k++) {
            uint64_t inc = (f * PR_RATIO_Q8[patch.osc[o].ratio[k] < PR_RATIOS ? patch.osc[o].ratio[k] : 1u]) >> 8;
            voice[v].inc[o][k] = inc > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)inc;   /* at most Nyquist */
        }
    }
}

static void engine_tables(uint32_t o, uint32_t k)   /* operator k of oscillator o: morph its two waves into RAM */
{
    uint32_t bank = patch.osc[o].bank[k] < PW_BANKS ? patch.osc[o].bank[k] : 0u;
    uint32_t q = (uint32_t)(patch.osc[o].pos[k] & 127u) * (PW_WAVES - 1u) * 256u / 127u, w = q >> 8, f = q & 255u, i;
    const int16_t *a, *c;
    int16_t *dst = op_wave[o][k][op_cur[o][k] == op_wave[o][k][0] ? 1 : 0];
    if (w >= PW_WAVES - 1u) { w = PW_WAVES - 2u; f = 256u; }
    a = PW_WAVE[bank][w];
    c = PW_WAVE[bank][w + 1u];
    for (i = 0; i < PW_N; i++)
        dst[i] = (int16_t)(a[i] + (((int32_t)c[i] - a[i]) * (int32_t)f >> 8));
    dst[PW_N] = dst[0];
    op_cur[o][k] = dst;
}

static void engine_set(const patch_t *p)        /* a new patch (or one parameter of it): everything derived again */
{
    uint32_t o, k, v, waves[NOSC][NOP];
    for (o = 0; o < NOSC; o++)
        for (k = 0; k < NOP; k++)
            waves[o][k] = !op_cur[o][k] || p->osc[o].bank[k] != patch.osc[o].bank[k] ||
                          p->osc[o].pos[k] != patch.osc[o].pos[k];
    patch = *p;
    for (o = 0; o < NOSC; o++) {
        for (k = 0; k < 2u; k++) {
            eg.m_att[o][k] = PR_ENV_ATT[patch.osc[o].att[k] & 127u];
            eg.m_rel[o][k] = PR_ENV_EXP[patch.osc[o].rel[k] & 127u];
            eg.m_amt[o][k] = PR_CURVE_Q15[patch.osc[o].amt[k] & 127u];
            eg.mode[o][k] = patch.osc[o].mode[k] < MODE_N ? patch.osc[o].mode[k] : MODE_FM;
        }
        eg.lvl[o] = (PR_CURVE_Q15[patch.osc[o].level & 127u] * PR_CURVE_Q15[(o ? patch.lvl2 : patch.lvl1) & 127u]) >> 15;
        for (k = 0; k < NOP; k++)
            if (waves[o][k])
                engine_tables(o, k);
    }
    eg.f_att = PR_ENV_ATT[patch.flt_a & 127u];
    eg.f_dec = PR_ENV_EXP[patch.flt_d & 127u];
    eg.f_sus = (int32_t)((patch.flt_s & 127u) * (uint32_t)(ENV_FULL / 127));
    eg.f_rel = PR_ENV_EXP[patch.flt_r & 127u];
    eg.f_cut = 64 + (int32_t)(patch.cut & 127u) * ((int32_t)PR_FILT_N - 1 - 64) / 127;   /* MIDI 16 .. 135.75 */
    eg.f_env = ((int32_t)(patch.fenv & 127u) - 64) * 240 / 63;                       /* +-60 semitones */
    eg.f_key = (int32_t)(patch.key & 127u) * 256 / 127;
    eg.f_k = PR_RES_K[patch.res & 127u];
    eg.drive = patch.drive ? PR_DRIVE_Q12[patch.drive & 127u] : 0;
    eg.a_att = PR_ENV_ATT[patch.amp_a & 127u];
    eg.a_dec = PR_ENV_EXP[patch.amp_d & 127u];
    eg.a_sus = (int32_t)((patch.amp_s & 127u) * (uint32_t)(ENV_FULL / 127));
    eg.a_rel = PR_ENV_EXP[patch.amp_r & 127u];
    for (v = 0; v < NV; v++)                    /* ratios and tuning may have changed: retune what sounds */
        if (voice[v].gate || voice[v].amp.st)
            voice_tune(v);
}

static void engine_init(void)
{
    uint32_t o, k;
    for (o = 0; o < NOSC; o++)                  /* force every table to be built */
        for (k = 0; k < NOP; k++)
            op_cur[o][k] = 0;
    engine_set(&PATCH_INIT);
}

/* ---- notes (main loop) */
static void engine_note_on(uint32_t note)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u)
        return;
    for (v = 0; v < NV; v++) {                  /* the same note, else a silent voice, else the quietest release,
                                                 * else the oldest held */
        uint32_t score = voice[v].note == note && voice[v].gate ? 0u
                       : !voice[v].amp.st && !voice[v].trig ? 1u
                       : !voice[v].gate ? 2u + ((uint32_t)voice[v].amp.lvl >> 8)
                       : 0x80000000u + voice[v].age;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = (uint8_t)note;
    voice[pick].age = ++ages;
    voice_tune(pick);                           /* the pitch before the gate: the ISR may run between */
    voice[pick].gate = 1;
    voice[pick].trig = 1;
}

static void engine_note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note)
            voice[v].gate = 0;
}

static int engine_quiet(void)                   /* nothing sounds, not even a release */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].gate || voice[v].trig || voice[v].amp.st)
            return 0;
    return 1;
}
