/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's sound engine: 4 voices, each 2 oscillators, each a wavetable carrier modulated by a series chain of two
 * wavetable modulators (mod 2 -> mod 1 -> carrier), each modulator with its own AR envelope and a mode (FM, RING,
 * MIX); the two oscillators mixed, driven, through a 2-pole low-pass with its own ADSR, then the amp ADSR. An LFO
 * per voice (or one for all, free-running) modulates two destinations: pitch, wave positions, mod amounts, the
 * modulators' pitch, cutoff, amp, the oscillators' mix. A mod envelope per voice (ADSR; LOOP: attack-decay again
 * and again until the key is let go) modulates one: the LFO's rate or depth, or any of the LFO's. Glide slides each
 * new note from the last one. The arpeggiator (in the ISR: its notes start and stop on the exact 16-frame block)
 * plays the held notes in turn.
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
enum { LFO_SINE, LFO_TRI, LFO_SAWUP, LFO_SAWDN, LFO_SQR, LFO_SH, LFO_N };
static const char *const LFO_NAME[LFO_N] = {"SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "S&H"};
enum { DEST_OFF, DEST_PITCH, DEST_PITCH2, DEST_WAVE1, DEST_WAVE2, DEST_MWAVE1, DEST_MWAVE2, DEST_MOD1, DEST_MOD2,
       DEST_MPITCH, DEST_CUTOFF, DEST_AMP, DEST_MIX,
       DEST_LFORATE, DEST_LFOAMT,               /* (the mod envelope's only: the LFO itself) */
       DEST_N };
enum { ARP_UP, ARP_DOWN, ARP_UPDN, ARP_RAND, ARP_ORDER, ARP_N };
static const char *const ARP_NAME[ARP_N] = {"UP", "DOWN", "UPDN", "RAND", "ORDER"};
enum { DIV_4, DIV_8, DIV_8T, DIV_16, DIV_16T, DIV_32, DIV_N };
static const char *const DIV_NAME[DIV_N] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const uint8_t DIV_SPB[DIV_N] = {1, 2, 3, 4, 6, 8};   /* steps per beat */
enum { GLIDE_ALWAYS, GLIDE_LEGATO, GLIDE_N };
static const char *const GLIDE_NAME[GLIDE_N] = {"ALWAYS", "LEGATO"};
static const char *const DEST_NAME[DEST_N] = {"OFF", "PITCH", "PITCH2", "WAVE1", "WAVE2", "MWAVE1", "MWAVE2",
                                              "MOD1", "MOD2", "MPITCH", "CUTOFF", "AMP", "MIX", "LFORATE", "LFOAMT"};

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
    /* (added in patch format 2: patches.c converts format 1, which ends here) */
    uint8_t lfo_wave;                           /* LFO_SINE .. LFO_SH */
    uint8_t lfo_rate;                           /* 0.05 .. 40 Hz */
    uint8_t lfo_delay;                          /* 0 = at once; else it fades in over 1 ms .. 10 s from the note */
    uint8_t lfo_retrig;                         /* 1: each note starts its own LFO; 0: one LFO runs free for all */
    uint8_t lfo_dest[2];                        /* DEST_OFF .. DEST_MIX */
    uint8_t lfo_amt[2];                         /* 64 = none; bipolar */
    /* (added in patch format 3: the effects, fx.c; a mix of 0 = off) */
    uint8_t cho_rate, cho_depth, cho_mix;
    uint8_t dly_time, dly_fb, dly_mix, dly_ping;
    uint8_t dly_wrate, dly_wdepth;              /* wow: the delay time swept */
    uint8_t dly_shift;                          /* each repeat: 0..24 = -12..+12 semitones (12 = none) */
    uint8_t dly_tone;                           /* the repeats: 0 dark .. 127 bright */
    uint8_t rev_size, rev_damp, rev_mix, rev_pre;
    uint8_t rev_mrate, rev_mdepth;              /* the tank's sweep: the tail's wobble */
    uint8_t rev_shimmer;                        /* the tail an octave up, back in */
    /* (added in patch format 4: the mod envelope) */
    uint8_t menv_a, menv_d, menv_s, menv_r;     /* ADSR */
    uint8_t menv_loop;                          /* 1: attack-decay over and over until the key is let go */
    uint8_t menv_dest;                          /* DEST_OFF .. DEST_LFOAMT */
    uint8_t menv_amt;                           /* 64 = none; bipolar */
    /* (added in patch format 5: velocity, from MIDI; the FM-1's own keys play at full velocity, so these change
     * nothing for them) */
    uint8_t vel_amp;                            /* how much softer notes are quieter */
    uint8_t vel_cut;                            /* how much softer notes are darker (up to 36 semitones) */
    uint8_t vel_mod;                            /* how much softer notes have less modulation (FM brightness) */
    /* (added in patch format 6: glide and the arpeggiator) */
    uint8_t glide;                              /* 0 = off; else the slide's time, 1 ms .. 10 s */
    uint8_t glide_mode;                         /* GLIDE_ALWAYS, GLIDE_LEGATO (only while another note is held) */
    uint8_t arp_on;
    uint8_t arp_mode;                           /* ARP_UP .. ARP_ORDER */
    uint8_t arp_div;                            /* DIV_4 .. DIV_32 */
    uint8_t arp_oct;                            /* 0..3 = 1..4 octaves */
    uint8_t arp_bpm;                            /* 0..200 = 40..240 BPM */
    uint8_t arp_gate;                           /* 0..127 = 5 .. 100 % of a step (127: tied) */
    uint8_t arp_latch;                          /* 1: the notes keep playing after the keys are let go */
    uint8_t arp_swing;                          /* 0..127 = none .. the even steps 3x the odd */
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
    .lfo_wave = LFO_SINE, .lfo_rate = 87, .lfo_delay = 0, .lfo_retrig = 1,   /* 5 Hz; no destination amount yet */
    .lfo_dest = {DEST_PITCH, DEST_CUTOFF}, .lfo_amt = {64, 64},
    .cho_rate = 40, .cho_depth = 60, .cho_mix = 0,             /* the effects: all there, all off (mix 0) */
    .dly_time = 50, .dly_fb = 60, .dly_mix = 0, .dly_ping = 1,
    .dly_wrate = 45, .dly_wdepth = 0, .dly_shift = 12, .dly_tone = 80,
    .rev_size = 85, .rev_damp = 50, .rev_mix = 0, .rev_pre = 15,
    .rev_mrate = 40, .rev_mdepth = 50, .rev_shimmer = 0,
    .menv_a = 60, .menv_d = 70, .menv_s = 0, .menv_r = 60, .menv_loop = 0,   /* no amount yet */
    .menv_dest = DEST_LFORATE, .menv_amt = 64,
    .vel_amp = 80, .vel_cut = 40, .vel_mod = 50,   /* (MIDI only: the keys play at full velocity) */
    .glide = 0, .glide_mode = GLIDE_ALWAYS,
    .arp_on = 0, .arp_mode = ARP_UP, .arp_div = DIV_16, .arp_oct = 0, .arp_bpm = 80, .arp_gate = 64,
    .arp_latch = 0, .arp_swing = 0,
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
    uint8_t pos[NOSC][NOP];                     /* wave positions (the LFO moves them per voice) */
    uint32_t lfo_inc, lfo_fade;                 /* LFO phase step per block; its fade-in step (Q24; full: at once) */
    uint8_t lfo_wave, lfo_retrig, lfo_dest[2];
    int32_t lfo_amt[2];                         /* Q15, signed (squared: fine at small amounts) */
    uint32_t me_att, me_dec, me_rel;            /* the mod envelope */
    int32_t me_sus;
    uint8_t me_loop, me_dest;
    int32_t me_amt;                             /* Q15, signed */
    int32_t vel_amp, vel_mod;                   /* velocity sensitivities, Q15 */
    int32_t vel_cut;                            /* the cutoff at velocity 0, quarter semitones below (0 .. 144) */
    uint32_t glide_k;                           /* the slide's coefficient a block (Q16; 0: no glide) */
    uint8_t glide_legato;
    uint8_t arp_on, arp_mode, arp_oct, arp_latch, arp_tie;
    uint32_t arp_len_q8;                        /* a step, frames Q8 */
    int32_t arp_gate;                           /* the note's share of its step, Q15 */
    int32_t arp_swing;                          /* 0 .. 16384: the even steps longer by it, the odd shorter */
} eg;

/* ---- the operators' waves: the morph of two neighbours, in RAM, double-buffered (engine_tables writes the one
 * the ISR is not reading, then swaps the pointer: a 32-bit store) */
static int16_t op_wave[NOSC][NOP][2][PW_N + 1];   /* + 1: a copy of [0], so the interpolation never wraps */
static const int16_t *volatile op_cur[NOSC][NOP];
/* ... and each operator's whole bank in RAM, for the LFO's wave-position destinations (each voice morphs its own) */
#ifdef __ELF__
#define ENGINE_POOL __attribute__((section(".pool")))   /* (the FM-1's big zeroed RAM; a Mac's Mach-O has no .pool) */
#else
#define ENGINE_POOL
#endif
static int16_t op_bank[NOSC][NOP][PW_WAVES][PW_N + 1] ENGINE_POOL;
static uint8_t op_bank_is[NOSC][NOP] = {{0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}};   /* the bank in op_bank */

/* ---- voices */
typedef struct { int32_t lvl; uint8_t st; } env_t;   /* st: 0 idle, 1 attack, 2 decay / hold, 3 release */
static struct {
    volatile uint32_t inc[NOSC][NOP];           /* phase steps (main loop) */
    volatile uint8_t gate;                      /* the key is down (main loop) */
    volatile uint8_t trig;                      /* a new note: the ISR restarts the envelopes (main sets, ISR clears) */
    uint32_t ph[NOSC][NOP];
    env_t amp, flt, mod[NOSC][2], menv;
    int32_t ic1, ic2;                           /* the filter's two integrator states */
    int32_t f_cs;                               /* the cutoff knob, smoothed (quarter semitones, Q8) */
    int32_t f_ks;                               /* the damping, smoothed (Q24) */
    int32_t fc[3];                              /* the coefficients the last block ended on (Q28) */
    int32_t amp_q15, mod_q15[NOSC][2];          /* the levels now (per frame, gliding to each block's target) */
    uint32_t lfo_ph, lfo_rng;                   /* its LFO (retrig on): phase, S&H's random sequence */
    int32_t lfo_sh, lfo_fade;                   /* the S&H value held; the fade-in (Q24) */
    int32_t rate16;                             /* the mod envelope's move of its LFO's rate, 1/16 semitones */
    uint8_t note;
    volatile uint8_t vel;                       /* 1 .. 127 (main loop, before the gate) */
    uint32_t age;
    int32_t glide_q8;                           /* the pitch still to slide, 1/16 semitones Q8 (to 0) */
    /* the arpeggiator's notes (ISR only): start / stop at block itrig / ioff of this render (0xFF: none) */
    uint8_t itrig, ioff, inote, ivel;
    int32_t iglide;
} voice[NV];

_Static_assert(BLK == 1u << BLK_SHIFT, "BLK is a power of two");
static patch_t patch;
static uint32_t ages;
static uint32_t lfo_ph, lfo_rng = 0x2545F491u;  /* the free-running LFO (retrig off) */
static int32_t lfo_sh;
static int32_t lfo_rate16;                      /* its rate moved by the newest note's mod envelope (last render) */
static int32_t last_note = -1;                  /* glide: the note the next one slides from */

/* ---- the arpeggiator: the held notes (the main loop's: arp_key_down / _up), read by the ISR each step */
#define ARP_MAX 16u
static volatile struct {
    uint8_t note[ARP_MAX], vel[ARP_MAX];        /* in the order they were pressed */
    uint32_t n;
    uint32_t seq;                               /* odd while the main loop changes them */
} held;
static uint32_t held_down;                      /* keys physically down (latch) */
static struct {
    uint32_t phase_q8, idx, n_was, odd, rng, now;   /* now: the next step at once (the first notes) */
    int32_t v;                                  /* its voice (-1: none) */
    int32_t gate_left;                          /* frames */
    volatile uint32_t steps;                    /* steps played (the page's picture follows it) */
} arp = {0, 0, 0, 0, 0x1234567u, 0, -1, 0, 0};

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

/* an operator's wave: its morph (a, f = 0), or, when the LFO moves its position, two of its bank's waves and the
 * share of the second (f: 0..256), morphed as it plays */
typedef struct { const int16_t *a, *b; int32_t f; } src_t;
static inline int32_t src_at(const src_t *s, uint32_t ph)
{
    int32_t v = wave_at(s->a, ph);
    if (s->f)
        v += ((wave_at(s->b, ph) - v) * s->f) >> 8;
    return v;
}

/* the wave t at phase ph under a modulator whose output is y (already scaled by its level e) */
static inline int32_t modulated(uint32_t mode, const src_t *t, uint32_t ph, int32_t y, int32_t e)
{
    int32_t s;
    if (mode == MODE_FM)                        /* phase modulation: y = full scale shifts the phase by 2 cycles */
        return src_at(t, ph + ((uint32_t)y << 18));
    s = src_at(t, ph);
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

/* ---- the LFO */
static int32_t lfo_wave(uint32_t w, uint32_t ph, int32_t sh)   /* -> -32767..32767 */
{
    int32_t x = (int32_t)(ph >> 16), ax, y, ay;   /* 0..65535 */
    switch (w) {
    case LFO_TRI: return (x < 32768 ? x * 2 - 32768 : (65535 - x) * 2 - 32767) * 32767 / 32768;
    case LFO_SAWUP: return x - 32768 + (x == 0);
    case LFO_SAWDN: return 32767 - x;
    case LFO_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case LFO_SH: return sh;
    default:                                    /* a sine (a refined parabola, ~0.1 %) */
        x = (int32_t)ph >> 16;
        ax = x < 0 ? -x : x;
        y = (x * (32768 - ax)) >> 13;
        ay = y < 0 ? -y : y;
        y += ((((y * ay) >> 15) - y) * 7373) >> 15;
        return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
    }
}

static int32_t lfo_random(uint32_t *r)          /* S&H: the next value of a sequence, -32767..32767 */
{
    *r = *r * 1664525u + 1013904223u;
    return (int32_t)(*r >> 16) - 32768 + (*r >> 16 == 0);
}

/* what the LFO does to a voice for a block: offsets, gathered from the two destinations */
typedef struct {
    int32_t pitch16[NOSC], mpitch16;            /* pitch: 1/16 semitones (carriers and mods / the mods only) */
    int32_t pos[NOSC][NOP];                     /* wave positions: + -127..127 */
    int32_t amt[NOSC];                          /* both mod amounts of an oscillator: Q15 */
    int32_t cut;                                /* cutoff: quarter semitones */
    int32_t amp;                                /* amp: a factor, Q15 */
    int32_t mix;                                /* towards osc 2 (+) or osc 1 (-), Q15 */
    int32_t vmod;                               /* velocity's factor on the mod amounts, Q15 */
} lfo_mod_t;

/* one source into m: s its value now (the LFO: -32767..32767; the mod envelope: 0..32767, uni), a the amount */
static void mod_add(lfo_mod_t *m, uint32_t dest, int32_t a, int32_t s, int uni)
{
    int32_t x = (s * a) >> 15, q = (x * 192) >> 15, pz = (x * 127) >> 15;
    if (!a)
        return;
    switch (dest) {
    case DEST_PITCH: m->pitch16[0] += q; m->pitch16[1] += q; break;      /* +-12 semitones */
    case DEST_PITCH2: m->pitch16[1] += q; break;
    case DEST_WAVE1: m->pos[0][0] += pz; break;                          /* the whole bank */
    case DEST_WAVE2: m->pos[1][0] += pz; break;
    case DEST_MWAVE1: m->pos[0][1] += pz; m->pos[0][2] += pz; break;
    case DEST_MWAVE2: m->pos[1][1] += pz; m->pos[1][2] += pz; break;
    case DEST_MOD1: m->amt[0] += x; break;                               /* +- the whole amount */
    case DEST_MOD2: m->amt[1] += x; break;
    case DEST_MPITCH: m->mpitch16 += q; break;
    case DEST_CUTOFF: m->cut += q; break;                                /* +-48 semitones */
    case DEST_AMP: {                                                     /* tremolo: down to 1 - |a| */
        int32_t aa = a < 0 ? -a : a, b = uni ? 2 * s - 32767 : s;   /* (an envelope: 0..1 as -1..1) */
        b = a < 0 ? -b : b;
        m->amp = (m->amp * (32767 - ((aa * (32767 - b)) >> 16))) >> 15;
        break;
    }
    case DEST_MIX: m->mix += x; break;
    default: break;
    }
}

static void lfo_mods(int32_t l, lfo_mod_t *m)   /* l: the LFO now (faded in), -32767..32767 */
{
    uint32_t d;
    memset(m, 0, sizeof *m);
    m->amp = 32767;
    for (d = 0; d < 2u; d++)
        mod_add(m, eg.lfo_dest[d], eg.lfo_amt[d], l, 0);
}

static uint32_t rate_moved(uint32_t inc, int32_t r16)   /* the LFO's phase step moved by r16 / 16 semitones (+-4 oct) */
{
    int32_t oct, rem;
    uint64_t v;
    if (!r16) return inc;
    r16 = r16 < -768 ? -768 : r16 > 768 ? 768 : r16;
    oct = r16 >= 0 ? r16 / 192 : -((-r16 + 191) / 192);
    rem = r16 - oct * 192;                      /* 0..191: a fraction of an octave */
    v = ((uint64_t)inc * PR_PITCH16_Q16[192 + rem]) >> 16;
    v = oct >= 0 ? v << oct : v >> -oct;
    return v > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)v;
}

static int32_t menv_step(env_t *e, uint32_t gate)   /* the mod envelope; LOOP: back to the attack after the decay */
{
    int32_t l = env_step(e, gate, eg.me_att, eg.me_dec, eg.me_sus, eg.me_rel);
    if (eg.me_loop && gate && e->st == 2u && l - eg.me_sus < ENV_FULL / 128)
        e->st = 1;
    return l;
}

#include "fx.c"                                 /* the effects: chorus, delay, reverb */

static void voice_tune(uint32_t v);
static void arp_clear(void);
static void engine_all_off(void);

/* ---- a voice starts (its note and pitch set): the envelopes (re)start from where they are */
static void voice_start(uint32_t v)
{
    uint32_t o;
    if (!voice[v].amp.st) {                     /* from silence: the phases from 0, the same attack every time */
        for (o = 0; o < NOSC; o++)
            voice[v].ph[o][0] = voice[v].ph[o][1] = voice[v].ph[o][2] = 0;
        voice[v].ic1 = voice[v].ic2 = 0;
        voice[v].f_cs = eg.f_cut << 8;          /* the knob as it is now; the first block starts on its own */
        voice[v].f_ks = (int32_t)eg.f_k;
        voice[v].fc[0] = -1;
    }
    voice[v].amp.st = 1;
    voice[v].flt.st = 1;
    for (o = 0; o < NOSC; o++)
        voice[v].mod[o][0].st = voice[v].mod[o][1].st = 1;
    voice[v].lfo_ph = 0;                        /* (used when the LFO retriggers) */
    voice[v].lfo_rng += 0x9E3779B9u + v;
    voice[v].lfo_sh = lfo_random(&voice[v].lfo_rng);
    voice[v].lfo_fade = 0;                      /* the delay: it fades in from the note */
    voice[v].menv.st = 1;
}

/* ---- which voice a new note takes: the same note, else a silent voice, else the quietest release, else the oldest
 * held; and where its glide starts (from the last note: always, or with LEGATO only while another note is held) */
static uint32_t voice_pick(uint32_t note)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    for (v = 0; v < NV; v++) {
        uint32_t busy = voice[v].gate || voice[v].trig || voice[v].itrig != 0xFFu;
        uint32_t score = voice[v].note == note && voice[v].gate ? 0u
                       : !voice[v].amp.st && !busy ? 1u
                       : !voice[v].gate && !busy ? 2u + ((uint32_t)voice[v].amp.lvl >> 8)
                       : 0x80000000u + voice[v].age;
        if (score < best) { best = score; pick = v; }
    }
    return pick;
}
static int32_t glide_from(uint32_t note)        /* the new note's slide (1/16 semitones Q8), then it is the last */
{
    uint32_t v, held_any = 0;
    int32_t g = 0;
    for (v = 0; v < NV; v++)
        held_any |= voice[v].gate;
    if (eg.glide_k && last_note >= 0 && (!eg.glide_legato || held_any))
        g = (last_note - (int32_t)note) * 16 * 256;
    last_note = (int32_t)note;
    return g;
}

/* ---- the arpeggiator (ISR) */
static uint32_t arp_pick(const uint8_t *nt, const uint8_t *vl, uint32_t n, uint32_t idx, uint32_t *vel, uint32_t *rng)
{                                               /* (rng: RAND's sequence; the page draws with its own) */
    uint8_t s[ARP_MAX], sv[ARP_MAX];
    uint32_t i, j, len = n * (eg.arp_oct + 1u), k, note;
    for (i = 0; i < n; i++) { s[i] = nt[i]; sv[i] = vl[i]; }
    if (eg.arp_mode != ARP_ORDER)               /* by pitch (insertion sort: a handful of notes) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
                uint8_t a = s[j]; s[j] = s[j - 1]; s[j - 1] = a;
                a = sv[j]; sv[j] = sv[j - 1]; sv[j - 1] = a;
            }
    switch (eg.arp_mode) {
    case ARP_DOWN: k = len - 1u - idx % len; break;
    case ARP_UPDN: {                            /* up, then down, the ends once */
        uint32_t l2 = len > 1u ? 2u * len - 2u : 1u;
        k = idx % l2;
        if (k >= len) k = l2 - k;
        break;
    }
    case ARP_RAND:
        *rng = *rng * 1664525u + 1013904223u;
        k = (*rng >> 8) % len;
        break;
    default: k = idx % len;                     /* UP, ORDER */
    }
    note = s[k % n] + 12u * (k / n);
    *vel = sv[k % n];
    return note > 127u ? 127u : note;
}

static uint32_t arp_step_len(uint32_t odd)      /* a step, frames Q8: swing makes the even ones longer */
{
    return (uint32_t)(((uint64_t)eg.arp_len_q8 * (uint32_t)(32768 + (odd ? -eg.arp_swing : eg.arp_swing))) >> 15);
}

static void arp_run(uint32_t nb)
{
    uint8_t nt[ARP_MAX], vl[ARP_MAX];
    uint32_t n, i, bi, s0;
    if (!eg.arp_on) {                           /* off: let its note go */
        if (arp.v >= 0) { voice[arp.v].ioff = 0; arp.v = -1; }
        arp.n_was = 0;
        return;
    }
    s0 = held.seq;                              /* the held notes: a copy no change was half-way through */
    n = held.n;
    if (n > ARP_MAX) n = ARP_MAX;
    for (i = 0; i < n; i++) { nt[i] = held.note[i]; vl[i] = held.vel[i]; }
    if ((s0 & 1u) || held.seq != s0)
        n = arp.n_was;                          /* (mid-change: as last time; next render it is settled) */
    if (n && !arp.n_was) {                      /* the first notes: the first step now */
        arp.now = 1;
        arp.idx = 0;
        arp.odd = 0;
    }
    for (bi = 0; bi < nb; bi++) {
        uint32_t len = arp_step_len(arp.odd);     /* the step running now (swing: the even ones long) */
        if (arp.v >= 0 && !eg.arp_tie) {        /* the gate: the note ends inside its step */
            arp.gate_left -= (int32_t)BLK;
            if (arp.gate_left <= 0) { voice[arp.v].ioff = (uint8_t)bi; arp.v = -1; }
        }
        if (!n) {                               /* no notes: stop (the step clock waits for the next) */
            if (arp.v >= 0) { voice[arp.v].ioff = (uint8_t)bi; arp.v = -1; }
            continue;
        }
        arp.phase_q8 += BLK << 8;
        if (arp.now || arp.phase_q8 >= len) {   /* a step */
            uint32_t vel, note, v;
            int32_t old = arp.v;
            if (arp.now) { arp.now = 0; arp.phase_q8 = 0; arp.odd = 1; }   /* (so the first step is an even one) */
            else arp.phase_q8 -= len;
            arp.odd ^= 1u;                      /* the step starting: its length sets the note's gate */
            len = arp_step_len(arp.odd);
            note = arp_pick(nt, vl, n, arp.idx++, &vel, &arp.rng);
            v = voice_pick(note);
            voice[v].inote = (uint8_t)note;
            voice[v].ivel = (uint8_t)(vel ? vel : 1u);
            voice[v].iglide = glide_from(note); /* (the old note still held: LEGATO slides at a tie) */
            voice[v].itrig = (uint8_t)bi;
            voice[v].age = ++ages;
            if (old >= 0 && (uint32_t)old != v) voice[old].ioff = (uint8_t)bi;
            arp.v = (int32_t)v;
            arp.gate_left = (int32_t)(((uint64_t)len * (uint32_t)eg.arp_gate) >> 23);   /* Q8 x Q15 -> frames */
            arp.steps++;
        }
    }
    arp.n_was = n;
}

/* ---- the audio ISR: add n stereo frames (L R) to out */
#define MAXBLK 32u                              /* blocks per render, at most (n <= 512) */
static void engine_render(int32_t *out, uint32_t n)
{
    uint32_t v, o, k, b, i, nb = n / BLK;
    uint32_t g_ph[MAXBLK];                      /* the free-running LFO, block by block, shared by the voices */
    int32_t g_sh[MAXBLK];
    for (b = 0; b < nb && b < MAXBLK; b++) {
        uint32_t nx = lfo_ph + rate_moved(eg.lfo_inc, lfo_rate16);
        if (nx < lfo_ph) lfo_sh = lfo_random(&lfo_rng);   /* a cycle ends: S&H takes a new value */
        lfo_ph = nx;
        g_ph[b] = lfo_ph;
        g_sh[b] = lfo_sh;
    }
    arp_run(nb);                                /* the arpeggiator: its notes for this render, block by block */
    for (v = 0; v < NV; v++) {
        if (voice[v].trig) {                    /* a note from the main loop: from the first block */
            voice[v].trig = 0;
            voice_start(v);
        }
        if (!voice[v].amp.st && voice[v].itrig == 0xFFu)
            continue;                           /* silent: costs nothing */
        for (b = 0; b < n; b += BLK) {
            uint32_t gate, bi = b / BLK < MAXBLK ? b / BLK : MAXBLK - 1u, ph;
            if (voice[v].ioff == b / BLK) {     /* the arpeggiator's: an end, then a start, on their blocks */
                voice[v].ioff = 0xFF;
                voice[v].gate = 0;
            }
            if (voice[v].itrig == b / BLK) {
                voice[v].itrig = 0xFF;
                voice[v].note = voice[v].inote;
                voice[v].vel = voice[v].ivel;
                voice[v].glide_q8 = voice[v].iglide;
                voice_tune(v);
                voice[v].gate = 1;
                voice_start(v);
            }
            if (!voice[v].amp.st)
                continue;
            gate = voice[v].gate;
            if (voice[v].glide_q8) {            /* glide: the rest of the slide, exponentially */
                voice[v].glide_q8 -= (int32_t)(((int64_t)voice[v].glide_q8 * eg.glide_k) >> 16);
                if (voice[v].glide_q8 > -64 && voice[v].glide_q8 < 64) voice[v].glide_q8 = 0;
            }
            int32_t a_to = env_step(&voice[v].amp, gate, eg.a_att, eg.a_dec, eg.a_sus, eg.a_rel) >> 9;
            int32_t a, da, l, acc[BLK];
            lfo_mod_t m;
            /* the mod envelope first (it may move the LFO's rate and depth) */
            int32_t me = menv_step(&voice[v].menv, gate) >> 9, me_x = (me * eg.me_amt) >> 15;
            voice[v].rate16 = eg.me_dest == DEST_LFORATE ? (me_x * 768) >> 15 : 0;   /* +-4 octaves */
            /* the LFO: this voice's own, or the free-running one; faded in; -> what it does this block */
            if (eg.lfo_retrig) {
                uint32_t nx = voice[v].lfo_ph + rate_moved(eg.lfo_inc, voice[v].rate16);
                if (nx < voice[v].lfo_ph) voice[v].lfo_sh = lfo_random(&voice[v].lfo_rng);
                voice[v].lfo_ph = nx;
                ph = nx;
                l = lfo_wave(eg.lfo_wave, ph, voice[v].lfo_sh);
            } else {
                ph = g_ph[bi];
                l = lfo_wave(eg.lfo_wave, ph, g_sh[bi]);
            }
            voice[v].lfo_fade += (int32_t)eg.lfo_fade;
            if (voice[v].lfo_fade > ENV_FULL) voice[v].lfo_fade = ENV_FULL;
            l = (l * (voice[v].lfo_fade >> 9)) >> 15;
            if (eg.me_dest == DEST_LFOAMT) {    /* the LFO's depth: x 0 .. 2 */
                int32_t f = 32767 + me_x;
                l = (l * (f < 0 ? 0 : f)) >> 15;
                l = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
            }
            lfo_mods(l, &m);
            if (eg.me_dest < DEST_LFORATE)      /* any of the LFO's destinations */
                mod_add(&m, eg.me_dest, eg.me_amt, me, 1);
            m.vmod = 32767;                     /* velocity: softer notes quieter (by its square), darker, less bright */
            if (voice[v].vel < 127u) {          /* (127, the keys: exactly as without velocity) */
                int32_t vel = voice[v].vel, vf = vel * 258, soft = 32767 - vf;   /* vf: 0 .. 32508 */
                if (eg.vel_amp)                 /* (each only with a sensitivity: x 32767 / 32768 is not x 1) */
                    m.amp = (m.amp * (32767 - ((eg.vel_amp * (32767 - ((vf * vf) >> 15))) >> 15))) >> 15;
                m.cut -= eg.vel_cut * (127 - vel) / 127;
                if (eg.vel_mod)
                    m.vmod = 32767 - ((eg.vel_mod * soft) >> 15);
            }
            a_to = (a_to * m.amp) >> 15;
            a = voice[v].amp_q15;
            da = (a_to - a) >> BLK_SHIFT;
            for (i = 0; i < BLK; i++)
                acc[i] = 0;
            for (o = 0; o < NOSC; o++) {
                src_t t[NOP];
                uint32_t p0 = voice[v].ph[o][0], p1 = voice[v].ph[o][1], p2 = voice[v].ph[o][2];
                int32_t g16 = voice[v].glide_q8 >> 8;   /* (+-48 semitones: the LFO, the envelope, glide) */
                uint32_t i0 = rate_moved(voice[v].inc[o][0], m.pitch16[o] + g16);
                uint32_t i1 = rate_moved(voice[v].inc[o][1], m.pitch16[o] + m.mpitch16 + g16);
                uint32_t i2 = rate_moved(voice[v].inc[o][2], m.pitch16[o] + m.mpitch16 + g16);
                uint32_t m1 = eg.mode[o][0], m2 = eg.mode[o][1];
                int32_t lvl = eg.lvl[o], am1, am2;
                int32_t e1_to, e2_to, e1, e2, d1, d2;
                if (m.mix)                      /* the LFO's MIX: one oscillator down as the other stays */
                    lvl = (lvl * (32767 - (o ? (m.mix < 0 ? -m.mix : 0) : (m.mix > 0 ? m.mix : 0)))) >> 15;
                am1 = eg.m_amt[o][0] + m.amt[o];
                am2 = eg.m_amt[o][1] + m.amt[o];
                am1 = am1 < 0 ? 0 : am1 > 32767 ? 32767 : am1;
                am2 = am2 < 0 ? 0 : am2 > 32767 ? 32767 : am2;
                if (m.vmod != 32767) {          /* velocity */
                    am1 = (am1 * m.vmod) >> 15;
                    am2 = (am2 * m.vmod) >> 15;
                }
                e1_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][0], gate, eg.m_att[o][0], 0, ENV_FULL,
                                                     eg.m_rel[o][0]) * am1) >> 24);
                e2_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][1], gate, eg.m_att[o][1], 0, ENV_FULL,
                                                     eg.m_rel[o][1]) * am2) >> 24);
                e1 = voice[v].mod_q15[o][0];
                e2 = voice[v].mod_q15[o][1];
                d1 = (e1_to - e1) >> BLK_SHIFT;
                d2 = (e2_to - e2) >> BLK_SHIFT;
                voice[v].mod_q15[o][0] = e1_to;
                voice[v].mod_q15[o][1] = e2_to;
                if (!lvl) {                     /* not heard: keep the phases going, skip the work */
                    voice[v].ph[o][0] = p0 + i0 * BLK;
                    voice[v].ph[o][1] = p1 + i1 * BLK;
                    voice[v].ph[o][2] = p2 + i2 * BLK;
                    continue;
                }
                for (k = 0; k < NOP; k++) {     /* the waves: the morph, or the bank morphed where the LFO puts it */
                    t[k].a = op_cur[o][k];
                    t[k].b = 0;
                    t[k].f = 0;
                    if (m.pos[o][k] && op_bank_is[o][k] == patch.osc[o].bank[k]) {
                        int32_t pp = (int32_t)eg.pos[o][k] + m.pos[o][k];
                        uint32_t q = (uint32_t)(pp < 0 ? 0 : pp > 127 ? 127 : pp) * (PW_WAVES - 1u) * 256u / 127u;
                        uint32_t w = q >> 8, f = q & 255u;
                        if (w >= PW_WAVES - 1u) { w = PW_WAVES - 2u; f = 256u; }
                        t[k].a = op_bank[o][k][w];
                        t[k].b = op_bank[o][k][w + 1u];
                        t[k].f = (int32_t)f;
                    }
                }
                if (!e2 && !e2_to) {            /* mod 2 silent: two operators */
                    for (i = 0; i < BLK; i++, e1 += d1) {
                        int32_t y1 = (src_at(&t[1], p1) * e1) >> 15;
                        acc[i] += (modulated(m1, &t[0], p0, y1, e1) * lvl) >> 15;
                        p0 += i0; p1 += i1;
                    }
                    p2 += i2 * BLK;
                } else {
                    for (i = 0; i < BLK; i++, e1 += d1, e2 += d2) {
                        int32_t y2 = (src_at(&t[2], p2) * e2) >> 15;
                        int32_t y1 = (modulated(m2, &t[1], p1, y2, e2) * e1) >> 15;
                        acc[i] += (modulated(m1, &t[0], p0, y1, e1) * lvl) >> 15;
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
                filter_coefs(voice[v].note, fe, voice[v].f_cs + (m.cut << 8), (uint32_t)voice[v].f_ks, f);
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
    {                                           /* the free-running LFO follows the newest note's mod envelope */
        uint32_t newest = NV, best = 0;
        for (v = 0; v < NV; v++)
            if (voice[v].amp.st && voice[v].age >= best) { best = voice[v].age; newest = v; }
        lfo_rate16 = newest < NV ? voice[newest].rate16 : 0;
    }
    fx_render(out, n);                          /* the effects, over all the voices */
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
    if (op_bank_is[o][k] != bank) {             /* the whole bank, for the LFO's per-voice positions */
        op_bank_is[o][k] = 0xFF;                /* (the ISR leaves it alone while it is copied) */
        for (w = 0; w < PW_WAVES; w++) {
            for (i = 0; i < PW_N; i++)
                op_bank[o][k][w][i] = PW_WAVE[bank][w][i];
            op_bank[o][k][w][PW_N] = PW_WAVE[bank][w][0];
        }
        op_bank_is[o][k] = (uint8_t)bank;
    }
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
    for (o = 0; o < NOSC; o++)
        for (k = 0; k < NOP; k++)
            eg.pos[o][k] = patch.osc[o].pos[k] & 127u;
    eg.lfo_inc = PR_LFO_INC[patch.lfo_rate & 127u];
    eg.lfo_fade = patch.lfo_delay ? PR_ENV_ATT[patch.lfo_delay & 127u] : (uint32_t)ENV_FULL;
    eg.lfo_wave = patch.lfo_wave < LFO_N ? patch.lfo_wave : LFO_SINE;
    eg.lfo_retrig = patch.lfo_retrig ? 1u : 0u;
    for (k = 0; k < 2u; k++) {
        int32_t d = (int32_t)(patch.lfo_amt[k] & 127u) - 64, mag = d < 0 ? -d : d;   /* 0..64 */
        int32_t q = PR_CURVE_Q15[mag * 127 / 64];
        eg.lfo_dest[k] = patch.lfo_dest[k] < DEST_LFORATE ? patch.lfo_dest[k] : DEST_OFF;   /* (not itself) */
        eg.lfo_amt[k] = eg.lfo_dest[k] == DEST_OFF ? 0 : d < 0 ? -q : q;
    }
    eg.me_att = PR_ENV_ATT[patch.menv_a & 127u];
    eg.me_dec = PR_ENV_EXP[patch.menv_d & 127u];
    eg.me_sus = (int32_t)((patch.menv_s & 127u) * (uint32_t)(ENV_FULL / 127));
    eg.me_rel = PR_ENV_EXP[patch.menv_r & 127u];
    eg.me_loop = patch.menv_loop ? 1u : 0u;
    {
        int32_t d = (int32_t)(patch.menv_amt & 127u) - 64, mag = d < 0 ? -d : d, q = PR_CURVE_Q15[mag * 127 / 64];
        eg.me_dest = patch.menv_dest < DEST_N ? patch.menv_dest : DEST_OFF;
        eg.me_amt = eg.me_dest == DEST_OFF ? 0 : d < 0 ? -q : q;
    }
    eg.vel_amp = (int32_t)(patch.vel_amp & 127u) * 32767 / 127;
    eg.vel_mod = (int32_t)(patch.vel_mod & 127u) * 32767 / 127;
    eg.vel_cut = (int32_t)(patch.vel_cut & 127u) * 144 / 127;
    eg.glide_k = patch.glide ? PR_ENV_EXP[patch.glide & 127u] : 0u;
    eg.glide_legato = patch.glide_mode == GLIDE_LEGATO;
    {
        uint32_t bpm = 40u + (patch.arp_bpm <= 200u ? patch.arp_bpm : 80u);
        uint32_t spb = DIV_SPB[patch.arp_div < DIV_N ? patch.arp_div : DIV_16];
        if (eg.arp_on != (patch.arp_on ? 1u : 0u)) {   /* on or off: the notes held go, the arp starts afresh */
            arp_clear();
            engine_all_off();
        }
        eg.arp_len_q8 = 44100u * 60u * 256u / (bpm * spb);
        eg.arp_mode = patch.arp_mode < ARP_N ? patch.arp_mode : ARP_UP;
        eg.arp_oct = patch.arp_oct <= 3u ? patch.arp_oct : 0u;
        eg.arp_latch = patch.arp_latch ? 1u : 0u;
        if (!eg.arp_latch && held_down == 0u && held.n) arp_clear();   /* latch off: the latched notes go */
        eg.arp_tie = (patch.arp_gate & 127u) == 127u;
        eg.arp_gate = 1638 + (int32_t)(patch.arp_gate & 127u) * (32768 - 1638) / 127;   /* 5 .. 100 % */
        eg.arp_swing = (int32_t)(patch.arp_swing & 127u) * 16384 / 127;
        eg.arp_on = patch.arp_on ? 1u : 0u;
    }
    fx_set(&patch);
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
    uint32_t o, k, v;
    fx_init();
    for (v = 0; v < NV; v++)
        voice[v].itrig = voice[v].ioff = 0xFF;
    for (o = 0; o < NOSC; o++)                  /* force every table to be built */
        for (k = 0; k < NOP; k++)
            op_cur[o][k] = 0;
    engine_set(&PATCH_INIT);
}

/* ---- notes (main loop) */
static void engine_note_on_vel(uint32_t note, uint32_t vel)   /* vel 1 .. 127 */
{
    uint32_t pick;
    if (note > 127u)
        return;
    pick = voice_pick(note);
    voice[pick].glide_q8 = glide_from(note);
    voice[pick].note = (uint8_t)note;
    voice[pick].vel = (uint8_t)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    voice[pick].age = ++ages;
    voice_tune(pick);                           /* the pitch before the gate: the ISR may run between */
    voice[pick].gate = 1;
    voice[pick].trig = 1;
}

static void engine_note_on(uint32_t note) { engine_note_on_vel(note, 127); }   /* the keys: full velocity */

static void engine_note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note)
            voice[v].gate = 0;
}

static int engine_quiet(void)                   /* nothing sounds, not even a release, and the arp is not playing */
{
    uint32_t v;
    if (eg.arp_on && held.n)
        return 0;
    for (v = 0; v < NV; v++)
        if (voice[v].gate || voice[v].trig || voice[v].itrig != 0xFFu || voice[v].amp.st)
            return 0;
    return 1;
}

/* ---- the arpeggiator's held notes (main loop): the keys and MIDI go here while it is on */
static void held_change(void) { held.seq++; }   /* called before and after a change: odd while changing (the ISR copies around it) */
static void arp_clear(void)
{
    held_change();
    held.n = 0;
    held_change();
    held_down = 0;
}
static void arp_key_down(uint32_t note, uint32_t vel)
{
    uint32_t i, n;
    if (eg.arp_latch && !held_down)             /* latch: a new chord, after all the keys were let go */
        arp_clear();
    held_down++;
    n = held.n;
    for (i = 0; i < n; i++)
        if (held.note[i] == note) return;       /* (already there: latched) */
    if (n >= ARP_MAX) return;
    held_change();
    held.note[n] = (uint8_t)note;
    held.vel[n] = (uint8_t)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    held.n = n + 1u;
    held_change();
}
static void arp_key_up(uint32_t note)
{
    uint32_t i, j, n = held.n;
    if (held_down) held_down--;
    if (eg.arp_latch)
        return;                                 /* latch: they keep playing */
    for (i = 0; i < n; i++)
        if (held.note[i] == note) {
            held_change();
            for (j = i; j + 1u < n; j++) { held.note[j] = held.note[j + 1u]; held.vel[j] = held.vel[j + 1u]; }
            held.n = n - 1u;
            held_change();
            return;
        }
}

static void engine_all_off(void)                /* every note let go (the arp switched on or off) */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        voice[v].gate = 0;
}
