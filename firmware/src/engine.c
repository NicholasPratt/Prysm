/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's sound engine: 4 voices, each 2 oscillators, each a wavetable carrier modulated by a series chain of two
 * wavetable modulators (mod 2 -> mod 1 -> carrier), each modulator with its own AR envelope and a mode (FM, RING,
 * MIX); the two oscillators mixed, driven, through the filter (with its own ADSR): a 2- or 4-pole low-pass, or a
 * comb (many resonant peaks, on the cutoff's frequency and its harmonics, SPREAD stretching the upper ones apart);
 * then a 2-pole high-pass (no resonance) and the amp ADSR. An LFO
 * per voice (or one for all, free-running) modulates two destinations: pitch, wave positions, mod amounts, the
 * modulators' pitch, cutoff, amp, the oscillators' mix, the comb's spread. LFO 2 (one for all, its rate free or in
 * time with the arp / sequencer's tempo) modulates one of those, or the filter's resonance, drive, high-pass, damping
 * and envelope amount, the levels, osc 2's detune, the mod envelope's amount, or the effects. A mod envelope per voice (ADSR; LOOP: attack-decay again
 * and again until the key is let go) modulates one: the LFO's rate or depth, or any of the LFO's. Glide slides each
 * new note from the last one. The arpeggiator (in the ISR: its notes start and stop on the exact 16-frame block)
 * plays the held notes in turn; the sequencer (beside it, on the same clock) plays up to 64 steps, 303-style: a
 * note, a rest or an accent each, and slides (the note held into the next, its pitch gliding, no new attack).
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
       DEST_SPREAD,                             /* (after: the saved numbers above stay) */
       DEST_M1PITCH, DEST_M2PITCH,              /* (format 10: mod 1's / mod 2's pitch; LFO 2's own move up 2) */
       DEST_LFOSHAPE,                           /* (format 11: the LFO's wave, morphed; not the LFO's own) */
       DEST_N,                                  /* (the LFO's and the mod envelope's end here; LFO 2's go on) */
       DEST_RES = DEST_N, DEST_DRIVE, DEST_HPF, DEST_DAMP, DEST_FENV, DEST_LVL1, DEST_LVL2, DEST_DETUNE, DEST_MENVAMT,
       DEST_CHORUS, DEST_DELAY, DEST_DLYTIME, DEST_DLYFB, DEST_REVERB, DEST_REVSIZE, DEST2_N };
#define DEST_FX(d) ((d) >= DEST_CHORUS)
enum { L2SYNC_OFF, L2SYNC_N = 12 };
static const char *const L2SYNC_NAME[L2SYNC_N] = {"OFF", "4/1", "2/1", "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T",
                                                  "1/16", "1/16T", "1/32"};
static const uint16_t L2SYNC_TICKS[L2SYNC_N] = {0, 384, 192, 96, 48, 24, 16, 12, 8, 6, 4, 3};   /* a cycle (24 a beat) */
enum { ARP_UP, ARP_DOWN, ARP_UPDN, ARP_RAND, ARP_ORDER, ARP_N };
static const char *const ARP_NAME[ARP_N] = {"UP", "DOWN", "UPDN", "RAND", "ORDER"};
enum { DIV_4, DIV_8, DIV_8T, DIV_16, DIV_16T, DIV_32, DIV_N };
static const char *const DIV_NAME[DIV_N] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const uint8_t DIV_SPB[DIV_N] = {1, 2, 3, 4, 6, 8};   /* steps per beat */
enum { GLIDE_ALWAYS, GLIDE_LEGATO, GLIDE_N };
static const char *const GLIDE_NAME[GLIDE_N] = {"ALWAYS", "LEGATO"};
enum { FLT_LP12, FLT_LP24, FLT_COMBP, FLT_COMBM, FLT_N };   /* the filter: COMB- (feedback inverted) is hollow */
static const char *const FLT_NAME[FLT_N] = {"LP 12", "LP 24", "COMB+", "COMB-"};
static const char *const DEST_NAME[DEST2_N] = {"OFF", "PITCH", "PITCH2", "WAVE1", "WAVE2", "MWAVE1", "MWAVE2",
                                              "MOD1", "MOD2", "MPITCH", "CUTOFF", "AMP", "MIX", "LFORATE", "LFOAMT",
                                              "SPREAD", "M1PITCH", "M2PITCH", "LFOSHAPE", "RES", "DRIVE", "HPF", "DAMP", "FENV", "LEVEL1", "LEVEL2",
                                              "DETUNE", "MENVAMT", "CHORUS", "DELAY", "DLYTIME", "DLYFDBK", "REVERB",
                                              "REVSIZE"};
#define DEST_LFO_OK(d) ((d) < DEST_N && (d) != DEST_LFORATE && (d) != DEST_LFOAMT && (d) != DEST_LFOSHAPE)   /* (not itself) */
#define DEST_V9_N 16u                           /* (format 9's DEST_N: its LFO 2 destinations from here are 3 lower) */
#define DEST_V10_N 18u                          /* (format 10's: from here 1 lower) */
#define DEST_LFO2_OK(d) ((d) < DEST2_N && (d) != DEST_LFORATE && (d) != DEST_LFOAMT)  /* LFO 2's: those, and more */

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
    /* (formats 7 .. 9 had a pattern here, seq_len .. seq_slide: patches.c, PATCH_SEQ_*; now seqs, below) */
    /* (added in patch format 8: the high-pass, the filter's type, the comb) */
    uint8_t hp_cut;                             /* the high-pass (12 dB/oct, no resonance): 0 = off; 16 Hz .. 4.2 kHz */
    uint8_t flt_type;                           /* FLT_LP12 .. FLT_COMBM; the comb: CUT its frequency, RES its feedback */
    uint8_t comb_damp;                          /* the repeats: 0 bright .. 127 dark */
    uint8_t comb_spread;                        /* 0 = even peaks; up: the upper ones stretched apart (bells, metal) */
    /* (added in patch format 9: LFO 2, one for all the voices) */
    uint8_t lfo2_wave;                          /* LFO_SINE .. LFO_SH */
    uint8_t lfo2_rate;                          /* 0.05 .. 40 Hz (SYNC OFF) */
    uint8_t lfo2_sync;                          /* L2SYNC_OFF, or a cycle in the arp / sequencer's tempo: 4/1 .. 1/32 */
    uint8_t lfo2_dest;                          /* DEST_OFF .. DEST2_N - 1 (not LFORATE, LFOAMT) */
    uint8_t lfo2_amt;                           /* 64 = none; bipolar */
    /* (added in patch format 10: mod envelope 2; the modulators' pitch) */
    uint8_t menv2_a, menv2_d, menv2_s, menv2_r; /* as the mod envelope's */
    uint8_t menv2_loop, menv2_dest, menv2_amt;
    uint8_t mpitch[NOSC][2];                    /* mod 1, mod 2 in MIX or RING (RATIO then): 0..48, 24 = the carrier's */
    uint8_t mfine[NOSC][2];                     /* mod 1, mod 2 (any mode): 64 = in tune, +-50 cents */
    /* (added in patch format 11: the VCA's bias) */
    uint8_t vca;                                /* the amp open by this much, whatever the envelope: 0 = closed; up: */
                                                /* every voice drones its last note at that level (Neutron's bias) */
} patch_t;
_Static_assert(__builtin_offsetof(patch_t, menv_amt) - __builtin_offsetof(patch_t, menv_a) == 6u &&
               __builtin_offsetof(patch_t, menv2_amt) - __builtin_offsetof(patch_t, menv2_a) == 6u,
               "a mod envelope's a d s r loop dest amt are in a row (engine_set)");
enum { SEQ_REST, SEQ_NOTE, SEQ_ACCENT, SEQ_TIE, SEQ_TRIGS };   /* (TIE: the note before goes on, no new attack) */
static const char *const SEQ_TRIG_NAME[SEQ_TRIGS] = {"REST", "NOTE", "ACCENT", "TIE"};

/* the sequencer's patterns and song: not part of a sound (patches.c keeps them in the store beside the bank). Four
 * patterns of up to 64 steps (each its own length; one step length and gate for all), and a song: up to 16 entries,
 * each a pattern played 1 .. 16 times, round and round. A step is a chord of up to SEQ_LANES notes: lane 0 is the
 * step's own (note, trig, slide: what the STEP row edits; ACCENT and SLIDE are the whole step's), lanes 1 .. 3 the
 * chord's others, each a note and a velocity, or a tie. Each lane plays as a voice of its own: a lane's TIE carries
 * that lane's note on. (Patterns of SEQS_VERSION 1 were lane 0 alone: patches.c converts them.) */
#define SEQ_PATS 4u
#define SONG_N 16u
#define SEQ_LANES 4u
#define SEQ_XTIE 0x80u                          /* xnote: the lane's note before goes on (no new attack) */
typedef struct {
    uint8_t len, pad[3];                        /* 1 .. 64 steps */
    uint8_t note[64], trig[64], slide[64];      /* lane 0; trig: SEQ_REST .. SEQ_TIE */
    uint8_t vel[64];                            /* lane 0's velocity (recorded): 0 = its trig's (NOTE 100) */
    uint8_t xnote[64][SEQ_LANES - 1u];          /* lanes 1 .. 3: a note (| SEQ_XTIE: a tie) */
    uint8_t xvel[64][SEQ_LANES - 1u];           /* their velocity; 0 and no tie: nothing in the lane */
} pattern_t;
typedef struct {
    uint32_t version;
    pattern_t pat[SEQ_PATS];
    uint8_t div, gate;                          /* the step (DIV_4 .. DIV_32) and its gate (0..127 = 5 .. 100 %) */
    uint8_t cur;                                /* the pattern edited, and played in PATTERN mode */
    uint8_t mode;                               /* 0: PATTERN (the pattern edited, over and over); 1: SONG */
    uint8_t song_len, pad2[3];                  /* 1 .. SONG_N entries */
    uint8_t song_pat[SONG_N], song_rep[SONG_N]; /* each entry: a pattern (0..3), played 1 .. 16 times */
} seqs_t;
#define SEQS_VERSION 2u
static seqs_t seqs;

static void seqs_init(void)                     /* four empty patterns (16 rests, C3 waiting), a song of P1 .. P4 */
{
    uint32_t i, k;
    memset(&seqs, 0, sizeof seqs);
    seqs.version = SEQS_VERSION;
    for (i = 0; i < SEQ_PATS; i++) {
        seqs.pat[i].len = 16;
        for (k = 0; k < 64u; k++) seqs.pat[i].note[k] = 48;
    }
    seqs.div = DIV_16;
    seqs.gate = 64;
    seqs.song_len = SEQ_PATS;
    for (i = 0; i < SONG_N; i++) { seqs.song_pat[i] = (uint8_t)(i % SEQ_PATS); seqs.song_rep[i] = 1; }
}
#define SEQ_VEL 100u                            /* a step's velocity (an accent: 127): AMP's VEL row makes the accent */
#define SEQ_SLIDE_K 58u                         /* a slide's speed (PR_ENV_EXP: there in ~30 ms, as a 303) */

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
    .hp_cut = 0, .flt_type = FLT_LP12, .comb_damp = 30, .comb_spread = 0,
    .lfo2_wave = LFO_TRI, .lfo2_rate = 60, .lfo2_sync = L2SYNC_OFF, .lfo2_dest = DEST_OFF, .lfo2_amt = 64,
    .menv2_a = 0, .menv2_d = 60, .menv2_s = 0, .menv2_r = 50, .menv2_loop = 0,   /* no amount yet */
    .menv2_dest = DEST_PITCH, .menv2_amt = 64,
    .mpitch = {{24, 24}, {24, 24}}, .mfine = {{64, 64}, {64, 64}},
    .vca = 0,
};

/* ---- derived from the patch by engine_set(), read by the ISR */
static volatile struct {
    uint32_t m_att[NOSC][2], m_rel[NOSC][2];    /* mod envelopes: attack step (Q24), release coefficient (Q16) */
    int32_t m_amt[NOSC][2];                     /* mod amounts, Q15 */
    uint8_t mode[NOSC][2];
    int32_t lvl[NOSC];                          /* oscillator output levels, Q15 */
    uint32_t a_att, a_dec, a_rel;               /* amp ADSR */
    int32_t vca;                                /* the amp's floor (VCA OPEN), Q15: 0 closed */
    int32_t a_sus;                              /* Q24 */
    uint32_t f_att, f_dec, f_rel;               /* filter ADSR */
    int32_t f_sus;
    int32_t f_cut;                              /* cutoff, quarter semitones (PR_FILT_G index) */
    int32_t f_env;                              /* envelope depth, quarter semitones at full envelope (+-) */
    int32_t f_key;                              /* keytrack, Q8 (256 = 100 %: a semitone per semitone) */
    int32_t f_k;                                /* damping, Q24 (under 0: self-oscillation) */
    int32_t drive;                              /* gain Q12; 0 = no drive stage */
    int32_t hp_g;                               /* the high-pass's one-poles: g / (1 + g), Q15; 0 = off */
    uint8_t flt_type;                           /* FLT_LP12 .. FLT_COMBM */
    int32_t comb_p16;                           /* the comb's CUT: MIDI 12 + 0.75 a step (64: C4, 80: C5), 1/16 st */
    int32_t comb_spread;                        /* 0..127 */
    int32_t comb_fb, comb_in;                   /* feedback (Q15, signed); the input's share, sqrt(1 - |fb|) */
    int32_t comb_dk;                            /* the damping one-pole in the loop, Q15 (32767: none) */
    uint8_t res, drv, hp, damp, lvl_mix[NOSC];  /* (the knobs, for LFO 2: RES, DRIVE, HPF, DAMP, LEVEL1 / 2) */
    int32_t lvl_osc[NOSC];                      /* the OSC pages' levels, Q15 (lvl = lvl_osc x the MIX page's) */
    uint8_t l2_wave, l2_sync, l2_dest;          /* LFO 2 */
    uint32_t l2_inc;                            /* its phase step a block (SYNC OFF) */
    int32_t l2_amt;                             /* Q15, signed (squared, as the LFO's) */
    uint32_t tick_q8;                           /* the arp's tempo: a MIDI clock tick (1/24 beat), frames Q8 */
    uint8_t pos[NOSC][NOP];                     /* wave positions (the LFO moves them per voice) */
    uint32_t lfo_inc, lfo_fade;                 /* LFO phase step per block; its fade-in step (Q24; full: at once) */
    uint8_t lfo_wave, lfo_retrig, lfo_dest[2];
    int32_t lfo_amt[2];                         /* Q15, signed (squared: fine at small amounts) */
    uint32_t me_att[2], me_dec[2], me_rel[2];   /* the mod envelopes */
    int32_t me_sus[2];
    uint8_t me_loop[2], me_dest[2];
    int32_t me_amt[2];                          /* Q15, signed */
    int32_t vel_amp, vel_mod;                   /* velocity sensitivities, Q15 */
    int32_t vel_cut;                            /* the cutoff at velocity 0, quarter semitones below (0 .. 144) */
    uint32_t glide_k;                           /* the slide's coefficient a block (Q16; 0: no glide) */
    uint8_t glide_legato;
    uint8_t arp_on, arp_mode, arp_oct, arp_latch, arp_tie;
    uint8_t arp_midi;                           /* 1: the arp follows the MIDI clock (a global setting: engine_sync) */
    uint32_t arp_spb;                           /* steps a beat (MIDI clock: 24 / it ticks a step) */
    uint32_t arp_len_q8;                        /* a step, frames Q8 */
    int32_t arp_gate;                           /* the note's share of its step, Q15 */
    int32_t arp_swing;                          /* 0 .. 16384: the even steps longer by it, the odd shorter */
    uint32_t seq_len_q8, seq_spb, seq_steps;    /* the sequencer: a step (frames Q8), steps a beat, its length */
    int32_t seq_gate;                           /* Q15 */
    volatile uint8_t seq_play;                  /* running (PLAY; the main loop's) */
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
/* ... and each voice's comb line: down to 21.5 Hz (in RAM: the pool is nearly full) */
#define COMB_N 2048u
#define COMB_MASK (COMB_N - 1u)
static int16_t comb_buf[NV][COMB_N];

static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9): f * 2^32 / 44100; lower octaves shift */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

/* ---- voices */
typedef struct { int32_t lvl; uint8_t st; } env_t;   /* st: 0 idle, 1 attack, 2 decay / hold, 3 release */
static struct {
    volatile uint32_t inc[NOSC][NOP];           /* phase steps (main loop) */
    volatile uint8_t gate;                      /* the key is down (main loop) */
    volatile uint8_t trig;                      /* a new note: the ISR restarts the envelopes (main sets, ISR clears) */
    uint32_t ph[NOSC][NOP];
    env_t amp, flt, mod[NOSC][2], menv[2];
    int32_t ic1, ic2, ic3, ic4;                 /* the filter's integrator states (3, 4: LP 24's first stage) */
    int32_t f_cs;                               /* the cutoff knob, smoothed (quarter semitones, Q8) */
    int32_t f_ks;                               /* the damping, smoothed (Q24) */
    int32_t f_pk;                               /* the band state's peak, falling (the top of RES: its level control) */
    int32_t fc[3], fc2[3];                      /* the coefficients the last block ended on (Q28; 2: LP 24's first) */
    int32_t hp1, hp2, hp_gs;                    /* the high-pass: its two one-poles' states; its coefficient, smoothed */
    int32_t cd_q8;                              /* the comb: its delay the last block ended on (frames Q8; 0: none) */
    int32_t clp, cfb, cdk, cin;                 /* its damping state; feedback, damping, the input's share (smoothed) */
    int32_t apx[4], apy[4];                     /* SPREAD's four allpasses: their last input, output */
    uint16_t cw;                                /* where it writes next */
    uint8_t comb_live;                          /* its line holds this note's sound (else cleared first) */
    int32_t amp_q15, mod_q15[NOSC][2];          /* the levels now (per frame, gliding to each block's target) */
    uint8_t drone;                              /* it has played a note: with VCA OPEN it sounds on, after its release, its
                                                 * envelopes but the amp's held as if its key were down */
    uint32_t lfo_ph, lfo_rng;                   /* its LFO (retrig on): phase, S&H's random sequence */
    int32_t lfo_sh, lfo_fade;                   /* the S&H value held; the fade-in (Q24) */
    int32_t rate16;                             /* the mod envelope's move of its LFO's rate, 1/16 semitones */
    uint8_t note;
    volatile uint8_t vel;                       /* 1 .. 127 (main loop, before the gate) */
    uint32_t age;
    int32_t glide_q8;                           /* the pitch still to slide, 1/16 semitones Q8 (to 0) */
    uint32_t gk;                                /* its speed (Q16 a block): the glide's, or a slide's */
    /* the arpeggiator's notes (ISR only): start / stop at block itrig / ioff of this render (0xFF: none) */
    uint8_t itrig, ioff, inote, ivel, islide;   /* (islide: the sequencer's legato move to inote, no new attack) */
    int32_t iglide;
} voice[NV];

_Static_assert(BLK == 1u << BLK_SHIFT, "BLK is a power of two");
static patch_t patch;
static uint32_t ages;
static volatile uint8_t engine_up;              /* engine_init has run: the ISR starts before it (platform.c) */
static uint32_t lfo_ph, lfo_rng = 0x2545F491u;  /* the free-running LFO (retrig off) */
static int32_t lfo_sh;
static int32_t lfo_rate16;                      /* its rate moved by the newest note's mod envelope (last render) */
static uint32_t l2_ph, l2_rng = 0x6C8E9CF5u;    /* LFO 2 (free-running, one for all) */
static int32_t l2_sh;
static volatile int32_t bend16;                 /* MIDI pitch bend, 1/16 semitones (the main loop's: engine_bend) */
static volatile uint8_t l2_restart;             /* the sequencer / MIDI clock started: LFO 2 from the top */
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

/* ---- the filter for a voice, once per block: the cutoff (the smoothed knob, the envelope, keytrack; quarter
 * semitones, Q8) ... */
#define CUT_MAX ((int32_t)(PR_FILT_N - 1u) << 8)
#define K_BUTTER 23726566                       /* sqrt 2, Q24: LP 24's first stage (no resonance of its own) */
#define K_SELF 1677722                          /* 0.1, Q24: damping under this, the level is held (the render) */
#define K_HELD 335544                           /* 0.02, Q24: the most it damps (above: RES's own) */
#define SELF_LVL 10000                          /* the band state's level self-oscillation settles a little above */
static int32_t cut_at(uint32_t note, int32_t env, int32_t cut_q8, int32_t f_env)
{
    return cut_q8 + (int32_t)(((int64_t)f_env * env) >> 16) + ((((int32_t)note - 60) * eg.f_key) << 2);
}

/* ... -> g, interpolated between the table's quarter semitones, then the zero-delay-feedback SVF's
 * a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 (Q28); with f2, LP 24's first stage's too (k: K_BUTTER). The render
 * glides from one block's to the next's. */
static void svf_coefs(uint32_t g, int32_t k, int32_t *f)
{
    uint32_t den = (uint32_t)(((1 << 24) + (((int64_t)g * ((int64_t)g + k)) >> 24)) >> 8);   /* (1 + g (g + k)), Q16 */
    f[0] = (int32_t)((0xFFFFFFFFu / den) << 12);                           /* Q16 -> Q28 */
    f[1] = (int32_t)(((int64_t)f[0] * g) >> 24);
    f[2] = (int32_t)(((int64_t)f[1] * g) >> 24);
}

static void filter_coefs(int32_t c, int32_t k, int32_t *f, int32_t *f2)
{
    uint32_t g, i, fr;
    if (c < 0) c = 0;
    if (c > CUT_MAX - 1) c = CUT_MAX - 1;
    i = (uint32_t)c >> 8;
    fr = (uint32_t)c & 255u;
    g = PR_FILT_G[i] + (((PR_FILT_G[i + 1u] - PR_FILT_G[i]) * fr) >> 8);
    svf_coefs(g, k, f);
    if (f2)
        svf_coefs(g, K_BUTTER, f2);
}

static int32_t glide(int32_t now, int32_t to)   /* one block's step of a ~12 ms glide (32 blocks) */
{
    int32_t d = (to - now) >> 5;
    return d ? now + d : to;
}

static uint32_t msb32(uint32_t v)               /* the highest set bit (0 for 0 and 1) */
{
    uint32_t n = 0;
    if (v >> 16) { v >>= 16; n += 16; }
    if (v >> 8) { v >>= 8; n += 8; }
    if (v >> 4) { v >>= 4; n += 4; }
    if (v >> 2) { v >>= 2; n += 2; }
    return n + (v >> 1);
}

static uint32_t isqrt32(uint32_t x)             /* floor(sqrt(x)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    for (; b; b >>= 2) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
    }
    return r;
}

static int32_t hp_q(uint32_t c)                 /* HPF CUT 1..127 -> quarter semitones: MIDI 12 .. 108 (16 Hz .. 4.2 kHz) */
{
    return 48 + ((int32_t)(c & 127u) - 1) * 384 / 126;
}

/* ---- the knobs -> the derived values (engine_set; and the ISR, LFO 2 moving them) */
static int32_t hp_g_of(uint32_t c)              /* G = g / (1 + g), Q15 (g = tan(pi fc / fs), Q24, under 2^23); 0: off */
{
    uint32_t g;
    if (!c) return 0;
    g = PR_FILT_G[hp_q(c)];
    return (int32_t)((g << 7) / (((1u << 24) + g) >> 8));
}

static void comb_of(uint32_t res, uint32_t type, int32_t *fb, int32_t *in)
{                                               /* RES -> the feedback, up to 0.98 (eased: the top rings) */
    int32_t r = 127 - (int32_t)(res & 127u), f = 32112 - 32112 * r * r / (127 * 127), i;
    *fb = type == FLT_COMBM ? -f : f;
    i = (int32_t)isqrt32((uint32_t)(32768 - f) << 15);   /* sqrt(1 - fb): the peaks up to +17 dB */
    *in = i > 32767 ? 32767 : i;
}

static int32_t damp_of(uint32_t d) { return 32767 - (int32_t)(d & 127u) * 30000 / 127; }
static int32_t drive_of(uint32_t d) { return d ? PR_DRIVE_Q12[d & 127u] : 0; }
static uint32_t knob_moved(uint32_t v, int32_t pz)   /* a knob 0..127 moved by pz, kept in range */
{
    int32_t x = (int32_t)v + pz;
    return x < 0 ? 0u : x > 127 ? 127u : (uint32_t)x;
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

/* wave w moved by x (Q15: +-32767 = the whole row of shapes, SINE .. S&H), each between two crossfaded (LFOSHAPE) */
static int32_t lfo_morph(uint32_t w, int32_t x, uint32_t ph, int32_t sh)
{
    int32_t p, a, b, f;
    if (!x)
        return lfo_wave(w, ph, sh);
    p = (int32_t)w * 256 + (int32_t)(((int64_t)x * ((LFO_N - 1) * 256)) >> 15);
    p = p < 0 ? 0 : p > (LFO_N - 1) * 256 ? (LFO_N - 1) * 256 : p;
    f = p & 255;
    a = lfo_wave((uint32_t)(p >> 8), ph, sh);
    if (!f)
        return a;
    b = lfo_wave((uint32_t)(p >> 8) + 1u, ph, sh);
    return a + (((b - a) * f) >> 8);
}

static int32_t lfo_random(uint32_t *r)          /* S&H: the next value of a sequence, -32767..32767 */
{
    *r = *r * 1664525u + 1013904223u;
    return (int32_t)(*r >> 16) - 32768 + (*r >> 16 == 0);
}

/* what the LFO does to a voice for a block: offsets, gathered from the two destinations */
typedef struct {
    int32_t pitch16[NOSC], mpitch16;            /* pitch: 1/16 semitones (carriers and mods / the mods only) */
    int32_t mp16[2];                            /* mod 1's, mod 2's pitch (both oscillators'), 1/16 semitones */
    int32_t pos[NOSC][NOP];                     /* wave positions: + -127..127 */
    int32_t amt[NOSC];                          /* both mod amounts of an oscillator: Q15 */
    int32_t cut;                                /* cutoff: quarter semitones */
    int32_t amp;                                /* amp: a factor, Q15 */
    int32_t mix;                                /* towards osc 2 (+) or osc 1 (-), Q15 */
    int32_t spread;                             /* the comb's spread, + -127..127 */
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
    case DEST_M1PITCH: m->mp16[0] += q; break;                           /* +-12 semitones */
    case DEST_M2PITCH: m->mp16[1] += q; break;
    case DEST_CUTOFF: m->cut += q; break;                                /* +-48 semitones */
    case DEST_AMP: {                                                     /* tremolo: down to 1 - |a| */
        int32_t aa = a < 0 ? -a : a, b = uni ? 2 * s - 32767 : s;   /* (an envelope: 0..1 as -1..1) */
        b = a < 0 ? -b : b;
        m->amp = (m->amp * (32767 - ((aa * (32767 - b)) >> 16))) >> 15;
        break;
    }
    case DEST_MIX: m->mix += x; break;
    case DEST_SPREAD: m->spread += pz; break;                            /* +- the whole knob */
    case DEST_DETUNE: m->pitch16[1] += (x * 16) >> 15; break;            /* osc 2: +-1 semitone */
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

static int32_t menv_step(env_t *e, uint32_t gate, uint32_t k)   /* mod envelope k; LOOP: the attack again after the decay */
{
    int32_t l = env_step(e, gate, eg.me_att[k], eg.me_dec[k], eg.me_sus[k], eg.me_rel[k]);
    if (eg.me_loop[k] && gate && e->st == 2u && l - eg.me_sus[k] < ENV_FULL / 128)
        e->st = 1;
    return l;
}

#define MAXBLK 32u                              /* blocks per render, at most (n <= 512) */
#include "fx.c"                                 /* the effects: chorus, delay, reverb */

static void voice_tune(uint32_t v);
static void arp_clear(void);
static void engine_all_off(void);

/* ---- a voice starts (its note and pitch set): the envelopes (re)start from where they are */
static int voice_on(uint32_t v)                 /* it sounds: its envelope runs, or VCA OPEN holds it (or fades it out) */
{
    return voice[v].amp.st || (voice[v].drone && (eg.vca || voice[v].amp_q15 > 16));
}

static void voice_fresh(uint32_t v)             /* from silence: the phases from 0, the filter as the knobs are now */
{
    uint32_t o;
    for (o = 0; o < NOSC; o++)
        voice[v].ph[o][0] = voice[v].ph[o][1] = voice[v].ph[o][2] = 0;
    voice[v].ic1 = voice[v].ic2 = voice[v].ic3 = voice[v].ic4 = 0;
    voice[v].f_cs = eg.f_cut << 8;              /* the knob as it is now; the first block starts on its own */
    voice[v].f_ks = (int32_t)eg.f_k;
    voice[v].fc[0] = -1;
    voice[v].hp1 = voice[v].hp2 = 0;
    voice[v].hp_gs = eg.hp_g;
    voice[v].cd_q8 = 0;
    voice[v].clp = 0;
    voice[v].cfb = eg.comb_fb; voice[v].cdk = eg.comb_dk; voice[v].cin = eg.comb_in;
    voice[v].comb_live = 0;
}

static void voice_start(uint32_t v)
{
    uint32_t o;
    if (!voice_on(v))                           /* from silence: the same attack every time (a drone: it goes on) */
        voice_fresh(v);
    voice[v].drone = 1;
    voice[v].amp.st = 1;
    voice[v].flt.st = 1;
    for (o = 0; o < NOSC; o++)
        voice[v].mod[o][0].st = voice[v].mod[o][1].st = 1;
    voice[v].lfo_ph = 0;                        /* (used when the LFO retriggers) */
    voice[v].lfo_rng += 0x9E3779B9u + v;
    voice[v].lfo_sh = lfo_random(&voice[v].lfo_rng);
    voice[v].lfo_fade = 0;                      /* the delay: it fades in from the note */
    voice[v].menv[0].st = voice[v].menv[1].st = 1;
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

/* ---- MIDI clock (the arp's CLOCK = MIDI): clock and transport, each timed in audio frames as it arrives
 * (engine_clock, from the USB poll), are played one render later at their own block: a constant latency of one
 * render (5.8 ms) and no jitter, whatever the main loop does. 24 ticks a beat. */
#define CLK_N 16u
static volatile struct { uint8_t st[CLK_N]; uint32_t at[CLK_N]; uint32_t w; } clk;   /* written by the USB ISR */
static uint32_t clk_r;                          /* read by the audio ISR */
static volatile uint32_t engine_frames;         /* frames rendered: where the next render starts */
static struct {
    int32_t count;                              /* ticks since START (-1: the next one is the first) */
    uint32_t running, last_at, period_q8;       /* a tick's length, frames Q8 (smoothed) */
    int32_t wait;                               /* swing: frames until a held-back odd step */
} mc = {-1, 0, 0, 0, -1};
static volatile uint32_t mc_bpm10;              /* the tempo the clock gives, x 10 (0: none lately) */

static void engine_clock(uint32_t st, uint32_t at_frame)   /* (the USB ISR) a clock / transport byte, its frame */
{
    uint32_t w = clk.w;
    clk.st[w % CLK_N] = (uint8_t)st;
    clk.at[w % CLK_N] = at_frame;
    clk.w = w + 1u;
}

/* one arp step at block bi: the next note of the pattern, its gate from len (frames Q8) */
static void arp_step(uint32_t bi, const uint8_t *nt, const uint8_t *vl, uint32_t n, uint32_t len)
{
    uint32_t vel, note, v;
    int32_t old = arp.v;
    note = arp_pick(nt, vl, n, arp.idx++, &vel, &arp.rng);
    v = voice_pick(note);
    voice[v].inote = (uint8_t)note;
    voice[v].ivel = (uint8_t)(vel ? vel : 1u);
    voice[v].iglide = glide_from(note);         /* (the old note still held: LEGATO slides at a tie) */
    voice[v].itrig = (uint8_t)bi;
    voice[v].age = ++ages;
    if (old >= 0 && (uint32_t)old != v) voice[old].ioff = (uint8_t)bi;
    arp.v = (int32_t)v;
    arp.gate_left = (int32_t)(((uint64_t)len * (uint32_t)eg.arp_gate) >> 23);   /* Q8 x Q15 -> frames */
    arp.steps++;
}

/* ---- the sequencer (ISR, beside the arp, on its clock): a step at a time from seqs (a pattern, or the song); a voice
 * a lane. A step with SLIDE, or one before a lane's TIE, holds that lane's note into the next */
typedef struct { uint8_t pat, pos, entry, rep; } seqpos_t;   /* a step: its pattern, its place, the song's entry, its round */
static struct {
    seqpos_t at;                                /* the step to play next */
    uint32_t was, phase_q8, odd, now;
    uint32_t hold[SEQ_LANES];                   /* the lane's note goes on into the next step */
    int32_t v[SEQ_LANES];                       /* each lane's voice (-1: none) */
    int32_t gate_left, count, wait;             /* count: ticks since START (the MIDI clock) */
    volatile uint32_t steps, playing, playing_pat, entry;   /* (the page follows: steps played, the step, its pattern, entry) */
    volatile seqpos_t now_at;                   /* the step playing (recording: where a key falls) */
    volatile uint32_t step_at, step_len;        /* when it started (engine_frames), its length (frames Q8) */
    volatile uint8_t rec_mute;                  /* lanes a key is recording into (the main loop's): they rest */
    uint32_t n;                                 /* steps since PLAY (the metronome's count) */
    volatile uint32_t ci;                       /* the count-in: its steps still to come (a bar's, from PLAY) */
    volatile uint8_t in_ci, want_ci;            /* counting in; PLAY with a count-in (the main loop's) */
} seq = {.v = {-1, -1, -1, -1}, .count = -1, .wait = -1};

/* lane l of step k: SEQ_REST, SEQ_NOTE or SEQ_TIE (an accent: a NOTE at 127), its note and velocity */
static uint32_t seq_lane(const pattern_t *p, uint32_t k, uint32_t l, uint32_t *note, uint32_t *vel)
{
    uint32_t t;
    k %= 64u;
    if (!l) {
        t = p->trig[k] < SEQ_TRIGS ? p->trig[k] : SEQ_REST;
        *note = p->note[k] & 127u;
        *vel = t == SEQ_ACCENT ? 127u : p->vel[k] ? p->vel[k] & 127u : SEQ_VEL;
        return t == SEQ_ACCENT ? SEQ_NOTE : t;
    }
    t = p->xnote[k][l - 1u];
    *note = t & 127u;
    *vel = p->xvel[k][l - 1u] & 127u;
    return t & SEQ_XTIE ? SEQ_TIE : *vel ? SEQ_NOTE : SEQ_REST;
}

/* ---- the metronome: a tick each beat while the sequencer plays (CLICK ON, or REC: only while recording), a
 * decaying sine, 1 kHz (the bar's first beat: 2 kHz), ~40 ms, added after the effects (dry) */
static volatile struct { uint8_t mode, rec, lvl; } clk_set;   /* (the main loop's: engine_click) */
static struct { int32_t at, left, amp; uint32_t ph, inc; } tick = {-1, 0, 0, 0, 0};
static void click_start(uint32_t bi, uint32_t bar)   /* (the ISR) a tick from block bi of this render */
{
    if (!seq.in_ci && (!clk_set.mode || (clk_set.mode == 2u && !clk_set.rec) || !clk_set.lvl))
        return;                                 /* (the count-in ticks whatever CLICK says) */
    tick.at = (int32_t)(bi * BLK);
    tick.left = 1764;
    tick.amp = (int32_t)(clk_set.lvl ? clk_set.lvl : 80u) * 180;   /* (127: ~0.7 of full scale) */
    tick.ph = 0;
    tick.inc = bar ? 194783632u : 97391816u;    /* 2 kHz, 1 kHz: f * 2^32 / 44100 */
}
static void click_render(int32_t *out, uint32_t n)
{
    uint32_t i = tick.at > 0 ? (uint32_t)tick.at : 0u;
    if (tick.at < 0 && tick.left <= 0)
        return;
    tick.at = -1;
    for (; i < n && tick.left > 0; i++, tick.left--) {
        int32_t s = (lfo_wave(LFO_SINE, tick.ph, 0) * tick.amp) >> 15;
        tick.ph += tick.inc;
        tick.amp -= tick.amp >> 9;              /* (~12 ms to 1/e) */
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
}

static void seq_off(uint32_t bi)                /* every lane's note ends (block bi) */
{
    uint32_t l;
    for (l = 0; l < SEQ_LANES; l++) {
        if (seq.v[l] >= 0) voice[seq.v[l]].ioff = (uint8_t)bi;
        seq.v[l] = -1;
        seq.hold[l] = 0;
    }
}

static uint32_t seq_len_of(uint32_t i)          /* pattern i's length */
{
    uint32_t n = seqs.pat[i < SEQ_PATS ? i : 0u].len;
    return n >= 1u && n <= 64u ? n : 16u;
}

static seqpos_t seq_adv(seqpos_t s)             /* the step after s: on in the pattern, then the song's next round */
{
    uint32_t n, r;
    if (++s.pos < seq_len_of(s.pat))
        return s;
    s.pos = 0;
    if (seqs.mode) {
        n = seqs.song_len >= 1u && seqs.song_len <= SONG_N ? seqs.song_len : 1u;
        r = seqs.song_rep[s.entry % SONG_N];
        if (++s.rep >= (r >= 1u && r <= 16u ? r : 1u)) {
            s.rep = 0;
            s.entry = (uint8_t)((s.entry + 1u) % n);
        }
        s.pat = (uint8_t)(seqs.song_pat[s.entry % SONG_N] % SEQ_PATS);
    } else
        s.pat = (uint8_t)(seqs.cur % SEQ_PATS);   /* (another pattern chosen: from its first step, in time) */
    return s;
}

static uint32_t seq_step_len(uint32_t odd)
{
    return (uint32_t)(((uint64_t)eg.seq_len_q8 * (uint32_t)(32768 + (odd ? -eg.arp_swing : eg.arp_swing))) >> 15);
}

static void seq_step(uint32_t bi, uint32_t len);
static void seq_tick(uint32_t bi, uint32_t len)  /* a step's time: a count-in's step (a tick each beat), or the next */
{
    uint32_t spb = eg.seq_spb ? eg.seq_spb : 4u;
    if (seq.ci) {
        uint32_t i = 4u * spb - seq.ci;         /* (its place in the bar) */
        seq.in_ci = 1;
        seq.step_at = engine_frames + bi * BLK;
        seq.step_len = len;
        if (i % spb == 0u) click_start(bi, i == 0u);
        seq.ci--;
        return;
    }
    seq.in_ci = 0;
    seq_step(bi, len);
}

static void seq_step(uint32_t bi, uint32_t len)  /* the next step at block bi; len (frames Q8) for its gate */
{
    const pattern_t *p = &seqs.pat[seq.at.pat % SEQ_PATS], *pn;
    uint32_t k = seq.at.pos % 64u, kn, l, accent = p->trig[k] == SEQ_ACCENT;
    seq.playing = k;
    seq.playing_pat = seq.at.pat;
    seq.entry = seq.at.entry;
    seq.now_at = seq.at;
    seq.step_at = engine_frames + bi * BLK;
    seq.step_len = len;
    seq.at = seq_adv(seq.at);
    pn = &seqs.pat[seq.at.pat % SEQ_PATS];
    kn = seq.at.pos % 64u;
    for (l = 0; l < SEQ_LANES; l++) {
        uint32_t note, vel, nn, nv, st = seq_lane(p, k, l, &note, &vel), tie_next = seq_lane(pn, kn, l, &nn, &nv) == SEQ_TIE;
        int32_t v = seq.v[l];
        if (seq.rec_mute >> l & 1u)             /* (a key is recording into this lane: it plays itself) */
            st = SEQ_REST, tie_next = 0;
        if (accent && st == SEQ_NOTE)
            vel = 127u;
        if (st == SEQ_TIE) {                    /* the lane's note before goes on (none: a rest) */
            if (v < 0 || !voice[v].gate) {
                if (v >= 0) voice[v].ioff = (uint8_t)bi;
                v = -1;
            }
        } else if (v >= 0 && seq.hold[l] && st != SEQ_REST && voice[v].gate) {   /* the last one slides into it */
            voice[v].inote = (uint8_t)note;
            voice[v].ivel = (uint8_t)vel;
            voice[v].islide = (uint8_t)bi;
            if (!l) last_note = (int32_t)note;
        } else {
            if (v >= 0) voice[v].ioff = (uint8_t)bi;
            v = -1;
            if (st != SEQ_REST) {
                uint32_t w = voice_pick(note);
                voice[w].inote = (uint8_t)note;
                voice[w].ivel = (uint8_t)vel;
                voice[w].iglide = l ? 0 : glide_from(note);   /* (a chord: its root glides, the rest land) */
                voice[w].itrig = (uint8_t)bi;
                voice[w].age = ++ages;
                v = (int32_t)w;
            }
        }
        seq.hold[l] = v >= 0 && (tie_next || (st != SEQ_REST && p->slide[k]));
        seq.v[l] = v;
    }
    seq.gate_left = (int32_t)(((uint64_t)len * (uint32_t)eg.seq_gate) >> 23);
    seq.steps++;
    if (seq.n % (eg.seq_spb ? eg.seq_spb : 4u) == 0u)   /* a beat: the metronome (the bar's first, higher) */
        click_start(bi, seq.n % (4u * (eg.seq_spb ? eg.seq_spb : 4u)) == 0u);
    seq.n++;
}

static void seq_block(uint32_t bi, uint32_t due) /* a block of the sequencer; due: a step's tick came (MIDI clock) */
{
    if (!eg.seq_play) {                         /* stopped: its notes go */
        seq_off(bi);
        seq.was = 0;
        return;
    }
    if (!seq.was) {                             /* PLAY: from the first step (of the pattern, or of the song) */
        seq.was = 1;
        l2_restart = 1;                         /* (LFO 2 too) */
        seq.at.pos = seq.at.entry = seq.at.rep = 0;
        seq.at.pat = (uint8_t)((seqs.mode ? seqs.song_pat[0] : seqs.cur) % SEQ_PATS);
        seq.n = 0;
        seq.ci = seq.want_ci && !eg.arp_midi ? 4u * (eg.seq_spb ? eg.seq_spb : 4u) : 0u;   /* (a bar) */
        seq.want_ci = 0;
        seq.in_ci = 0;
        seq.now = 1;
        seq.odd = 0;
        seq.count = -1;
        seq.wait = -1;
    }
    seq.gate_left -= (int32_t)BLK;              /* the gate (a slide or a tie holds a lane into the next step) */
    if (seq.gate_left <= 0) {
        uint32_t l;
        for (l = 0; l < SEQ_LANES; l++)
            if (seq.v[l] >= 0 && !seq.hold[l]) { voice[seq.v[l]].ioff = (uint8_t)bi; seq.v[l] = -1; }
    }
    if (eg.arp_midi) {                          /* on the MIDI clock: the step's tick (an odd one swung later) */
        uint32_t slen = mc.period_q8 ? mc.period_q8 * (24u / (eg.seq_spb ? eg.seq_spb : 4u))
                                     : eg.seq_len_q8;   /* (the first tick: no tick length yet, its own tempo's) */
        if (due) {
            uint32_t tps = 24u / (eg.seq_spb ? eg.seq_spb : 4u), odd = (uint32_t)(seq.count / (int32_t)tps) & 1u;
            seq.wait = odd ? (int32_t)(((uint64_t)slen * (uint32_t)eg.arp_swing) >> 23) : 0;
            seq.odd = odd;
        }
        if (seq.wait >= 0) {
            if (seq.wait < (int32_t)BLK) {
                seq.wait = -1;
                seq_step(bi, (uint32_t)(((uint64_t)slen * (uint32_t)(32768 + (seq.odd ? -eg.arp_swing : eg.arp_swing))) >> 15));
            } else
                seq.wait -= (int32_t)BLK;
        }
        return;
    }
    seq.phase_q8 += BLK << 8;                   /* its own clock (the arp's tempo) */
    if (seq.now || seq.phase_q8 >= seq_step_len(seq.odd)) {
        if (seq.now) { seq.now = 0; seq.phase_q8 = 0; seq.odd = 1; }
        else seq.phase_q8 -= seq_step_len(seq.odd);
        seq.odd ^= 1u;
        seq_tick(bi, seq_step_len(seq.odd));
    }
}

static void arp_run(uint32_t nb)
{
    uint8_t nt[ARP_MAX], vl[ARP_MAX];
    uint32_t n, i, bi, s0, start = engine_frames, tps = 24u / (eg.arp_spb ? eg.arp_spb : 4u);   /* ticks a step */
    s0 = held.seq;                              /* the held notes: a copy no change was half-way through */
    n = held.n;
    if (n > ARP_MAX) n = ARP_MAX;
    for (i = 0; i < n; i++) { nt[i] = held.note[i]; vl[i] = held.vel[i]; }
    if ((s0 & 1u) || held.seq != s0)
        n = arp.n_was;                          /* (mid-change: as last time; next render it is settled) */
    if (!eg.arp_on) {                           /* off: let its note go (the clock still read: its tempo shows) */
        if (arp.v >= 0) { voice[arp.v].ioff = 0; arp.v = -1; }
        n = 0;
    }
    if (n && !arp.n_was && !eg.arp_midi) {      /* the first notes, on its own clock: the first step now */
        arp.now = 1;
        arp.idx = 0;
        arp.odd = 0;
    }
    if (mc.last_at && (int32_t)(start - mc.last_at) > 22050)
        mc_bpm10 = 0;                           /* no clock for half a second */
    for (bi = 0; bi < nb; bi++) {
        uint32_t len = arp_step_len(arp.odd), due = 0, sdue = 0;   /* (its own clock: the step running now) */
        while (clk_r != clk.w) {                /* MIDI clock: the events of this block (one render back) */
            uint32_t k = clk_r % CLK_N, at = clk.at[k], st = clk.st[k];
            int32_t rel = (int32_t)(at + nb * BLK - start), eb = rel < 0 ? 0 : rel / (int32_t)BLK;
            if (eb > (int32_t)bi && eb < (int32_t)nb) break;   /* (a later block) */
            clk_r++;
            if (st == 0xF8u) {                  /* a tick: the tempo; a step every tps of them */
                if (mc.last_at) {
                    uint32_t d = at - mc.last_at;
                    if (d > 0u && d < 44100u && (!mc.period_q8 || d < mc.period_q8 >> 7)) {   /* (the average of the
                                                 * last few; not a pause: a gap over twice a tick) */
                        int32_t now = (int32_t)(d << 8), was = (int32_t)mc.period_q8;
                        mc.period_q8 = (uint32_t)(was ? was + (now - was) / 8 : now);
                    }
                }
                mc.last_at = at;
                if (mc.period_q8) mc_bpm10 = 1102500u * 256u / mc.period_q8;   /* 60 * 44100 * 10 / 24 / period */
                if (mc.running && eg.arp_midi) {
                    mc.count++;
                    if (mc.count % (int32_t)tps == 0) due = 1;
                    if (eg.seq_play) {          /* the sequencer's ticks */
                        uint32_t st2 = 24u / (eg.seq_spb ? eg.seq_spb : 4u);
                        seq.count++;
                        if (seq.count % (int32_t)st2 == 0) sdue = 1;
                    }
                }
            } else if (st == 0xFAu) {           /* START: from the top, on the next tick */
                mc.running = 1; mc.count = -1; mc.wait = -1;
                mc.last_at = 0;                 /* (no tempo across the pause) */
                arp.idx = 0;
                seq.at.pos = seq.at.entry = seq.at.rep = 0;   /* (the sequencer: from its first step) */
                seq.at.pat = (uint8_t)((seqs.mode ? seqs.song_pat[0] : seqs.cur) % SEQ_PATS);
                seq.count = -1; seq.wait = -1;
                l2_restart = 1;
            } else if (st == 0xFBu) {           /* CONTINUE */
                mc.running = 1;
            } else if (st == 0xFCu) {           /* STOP: and its note */
                mc.running = 0; mc.wait = -1; seq.wait = -1;
                if (eg.arp_midi && arp.v >= 0) { voice[arp.v].ioff = (uint8_t)bi; arp.v = -1; }
                if (eg.arp_midi) seq_off(bi);
            }
        }
        seq_block(bi, sdue);                    /* the sequencer, this block */
        if (arp.v >= 0 && !eg.arp_tie) {        /* the gate: the note ends inside its step */
            arp.gate_left -= (int32_t)BLK;
            if (arp.gate_left <= 0) { voice[arp.v].ioff = (uint8_t)bi; arp.v = -1; }
        }
        if (!n) {                               /* no notes: stop (the step clock waits for the next) */
            if (arp.v >= 0) { voice[arp.v].ioff = (uint8_t)bi; arp.v = -1; }
            mc.wait = -1;
            continue;
        }
        if (eg.arp_midi) {                      /* on the MIDI clock: a step on its tick (an odd one: swung later) */
            uint32_t slen = mc.period_q8 ? mc.period_q8 * tps : eg.arp_len_q8;   /* (the first tick: its own) */
            if (due) {
                uint32_t odd = (uint32_t)(mc.count / (int32_t)tps) & 1u;
                mc.wait = odd ? (int32_t)(((uint64_t)slen * (uint32_t)eg.arp_swing) >> 23) : 0;
                arp.odd = odd;
            }
            if (mc.wait >= 0) {
                if (mc.wait < (int32_t)BLK) {
                    mc.wait = -1;
                    arp_step(bi, nt, vl, n, (uint32_t)(((uint64_t)slen * (uint32_t)(32768 + (arp.odd ? -eg.arp_swing : eg.arp_swing))) >> 15));
                } else
                    mc.wait -= (int32_t)BLK;
            }
            continue;
        }
        arp.phase_q8 += BLK << 8;               /* on its own clock: a step when the step's frames are done */
        if (arp.now || arp.phase_q8 >= len) {
            if (arp.now) { arp.now = 0; arp.phase_q8 = 0; arp.odd = 1; }   /* (so the first step is an even one) */
            else arp.phase_q8 -= len;
            arp.odd ^= 1u;                      /* the step starting: its length sets the note's gate */
            arp_step(bi, nt, vl, n, arp_step_len(arp.odd));
        }
    }
    arp.n_was = n;
}

/* ---- the audio ISR: add n stereo frames (L R) to out */
static void engine_render(int32_t *out, uint32_t n)
{
    uint32_t v, o, k, b, i, nb = n / BLK;
    uint32_t g_ph[MAXBLK];                      /* the free-running LFO, block by block, shared by the voices */
    int32_t g_sh[MAXBLK];
    int32_t g2[MAXBLK];                         /* LFO 2, block by block: -32767..32767 */
    uint32_t l2inc = eg.l2_inc;
    if (!engine_up)                             /* (zeroed voices: itrig 0 would start note 0 on every one, held) */
        return;
    if (eg.l2_sync) {                           /* in time: a cycle is so many ticks of the arp's (or MIDI's) tempo */
        uint32_t t = eg.arp_midi && mc_bpm10 && mc.period_q8 ? mc.period_q8 : eg.tick_q8, bq8;
        t = t > 1000000u ? 1000000u : t;        /* (under ~28 BPM: as slow as that) */
        bq8 = (t * L2SYNC_TICKS[eg.l2_sync]) >> BLK_SHIFT;   /* blocks a cycle, Q8 */
        l2inc = (0xFFFFFFFFu / (bq8 ? bq8 : 1u)) << 8;
    }
    if (l2_restart) {                           /* (the sequencer or the MIDI clock started: from the top) */
        l2_restart = 0;
        l2_ph = 0;
        l2_sh = lfo_random(&l2_rng);
    }
    for (b = 0; b < nb && b < MAXBLK; b++) {
        uint32_t nx = lfo_ph + rate_moved(eg.lfo_inc, lfo_rate16);
        if (nx < lfo_ph) lfo_sh = lfo_random(&lfo_rng);   /* a cycle ends: S&H takes a new value */
        lfo_ph = nx;
        g_ph[b] = lfo_ph;
        g_sh[b] = lfo_sh;
        g2[b] = lfo_wave(eg.l2_wave, l2_ph, l2_sh);
        nx = l2_ph + l2inc;
        if (nx < l2_ph) l2_sh = lfo_random(&l2_rng);
        l2_ph = nx;
    }
    arp_run(nb);                                /* the arpeggiator: its notes for this render, block by block */
    for (v = 0; v < NV; v++) {
        if (voice[v].trig) {                    /* a note from the main loop: from the first block */
            voice[v].trig = 0;
            voice_start(v);
        }
        if (!voice_on(v) && voice[v].itrig == 0xFFu) {
            voice[v].islide = 0xFF;             /* (a slide into a voice gone quiet: nothing to slide) */
            continue;                           /* silent: costs nothing */
        }
        for (b = 0; b < n; b += BLK) {
            uint32_t gate, bi = b / BLK < MAXBLK ? b / BLK : MAXBLK - 1u, ph;
            if (voice[v].ioff == b / BLK) {     /* the arpeggiator's: an end, then a start, on their blocks */
                voice[v].ioff = 0xFF;
                voice[v].gate = 0;
            }
            if (voice[v].islide == b / BLK) {   /* a slide: the new note's pitch, gliding there, no new attack */
                voice[v].islide = 0xFF;
                voice[v].glide_q8 += ((int32_t)voice[v].note - (int32_t)voice[v].inote) * 16 * 256;
                voice[v].gk = PR_ENV_EXP[SEQ_SLIDE_K];
                voice[v].note = voice[v].inote;
                voice[v].vel = voice[v].ivel;
                voice_tune(v);
            }
            if (voice[v].itrig == b / BLK) {
                voice[v].itrig = 0xFF;
                voice[v].note = voice[v].inote;
                voice[v].vel = voice[v].ivel;
                voice[v].glide_q8 = voice[v].iglide;
                voice[v].gk = eg.glide_k;
                voice_tune(v);
                voice[v].gate = 1;
                voice_start(v);
            }
            if (!voice_on(v))
                continue;
            gate = voice[v].gate;
            if (voice[v].glide_q8) {            /* glide: the rest of the slide, exponentially */
                voice[v].glide_q8 -= (int32_t)(((int64_t)voice[v].glide_q8 * voice[v].gk) >> 16);
                if (voice[v].glide_q8 > -64 && voice[v].glide_q8 < 64) voice[v].glide_q8 = 0;
            }
            int32_t a_to = env_step(&voice[v].amp, gate, eg.a_att, eg.a_dec, eg.a_sus, eg.a_rel) >> 9;
            uint32_t hold = gate || (eg.vca && voice[v].drone);   /* (a drone: its other envelopes held, as if down) */
            int32_t a, da, l, acc[BLK];
            lfo_mod_t m;
            /* the mod envelope first (it may move the LFO's rate and depth) */
            /* LFO 2 this block: x2 its value times its amount (Q15), pz2 that over a knob's range */
            int32_t x2 = (g2[bi] * eg.l2_amt) >> 15, pz2 = (x2 * 127) >> 15, me_amt = eg.me_amt[0];
            if (eg.l2_dest == DEST_MENVAMT) {   /* (the mod envelope's amount: +- the whole of it) */
                me_amt += x2;
                me_amt = me_amt > 32767 ? 32767 : me_amt < -32767 ? -32767 : me_amt;
            }
            int32_t me = menv_step(&voice[v].menv[0], hold, 0) >> 9, me_x = (me * me_amt) >> 15;
            int32_t me2 = menv_step(&voice[v].menv[1], hold, 1) >> 9, me2_x = (me2 * eg.me_amt[1]) >> 15;
            voice[v].rate16 = (((eg.me_dest[0] == DEST_LFORATE ? me_x : 0) +   /* +-4 octaves (each) */
                                (eg.me_dest[1] == DEST_LFORATE ? me2_x : 0)) * 768) >> 15;
            /* the LFO: this voice's own, or the free-running one; its shape moved (LFOSHAPE); faded in; -> what it
             * does this block */
            int32_t shp = (eg.me_dest[0] == DEST_LFOSHAPE ? me_x : 0) + (eg.me_dest[1] == DEST_LFOSHAPE ? me2_x : 0) +
                          (eg.l2_dest == DEST_LFOSHAPE ? x2 : 0);
            if (eg.lfo_retrig) {
                uint32_t nx = voice[v].lfo_ph + rate_moved(eg.lfo_inc, voice[v].rate16);
                if (nx < voice[v].lfo_ph) voice[v].lfo_sh = lfo_random(&voice[v].lfo_rng);
                voice[v].lfo_ph = nx;
                ph = nx;
                l = lfo_morph(eg.lfo_wave, shp, ph, voice[v].lfo_sh);
            } else {
                ph = g_ph[bi];
                l = lfo_morph(eg.lfo_wave, shp, ph, g_sh[bi]);
            }
            voice[v].lfo_fade += (int32_t)eg.lfo_fade;
            if (voice[v].lfo_fade > ENV_FULL) voice[v].lfo_fade = ENV_FULL;
            l = (l * (voice[v].lfo_fade >> 9)) >> 15;
            if (eg.me_dest[0] == DEST_LFOAMT) { /* the LFO's depth: x 0 .. 2 */
                int32_t f = 32767 + me_x;
                l = (l * (f < 0 ? 0 : f)) >> 15;
                l = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
            }
            if (eg.me_dest[1] == DEST_LFOAMT) {
                int32_t f = 32767 + me2_x;
                l = (l * (f < 0 ? 0 : f)) >> 15;
                l = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
            }
            lfo_mods(l, &m);
            if (DEST_LFO_OK(eg.me_dest[0]))     /* any of the LFO's destinations */
                mod_add(&m, eg.me_dest[0], me_amt, me, 1);
            if (DEST_LFO_OK(eg.me_dest[1]))
                mod_add(&m, eg.me_dest[1], eg.me_amt[1], me2, 1);
            mod_add(&m, eg.l2_dest, eg.l2_amt, g2[bi], 0);   /* LFO 2 (on the LFO's destinations; DETUNE) */
            m.pitch16[0] += bend16;             /* pitch bend: every note (and the comb's keytrack with it) */
            m.pitch16[1] += bend16;
            m.vmod = 32767;                     /* velocity: softer notes quieter (by its square), darker, less bright */
            if (voice[v].vel < 127u) {          /* (127, the keys: exactly as without velocity) */
                int32_t vel = voice[v].vel, vf = vel * 258, soft = 32767 - vf;   /* vf: 0 .. 32508 */
                if (eg.vel_amp)                 /* (each only with a sensitivity: x 32767 / 32768 is not x 1) */
                    m.amp = (m.amp * (32767 - ((eg.vel_amp * (32767 - ((vf * vf) >> 15))) >> 15))) >> 15;
                m.cut -= eg.vel_cut * (127 - vel) / 127;
                if (eg.vel_mod)
                    m.vmod = 32767 - ((eg.vel_mod * soft) >> 15);
            }
            if (eg.vca)                         /* VCA OPEN: the envelope over a floor */
                a_to = eg.vca + ((a_to * (32767 - eg.vca)) >> 15);
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
                uint32_t i1 = rate_moved(voice[v].inc[o][1], m.pitch16[o] + m.mpitch16 + m.mp16[0] + g16);
                uint32_t i2 = rate_moved(voice[v].inc[o][2], m.pitch16[o] + m.mpitch16 + m.mp16[1] + g16);
                uint32_t m1 = eg.mode[o][0], m2 = eg.mode[o][1];
                int32_t lvl = eg.lvl[o], am1, am2;
                if (eg.l2_dest == DEST_LVL1 + o)   /* LFO 2 on the MIX page's level */
                    lvl = (eg.lvl_osc[o] * PR_CURVE_Q15[knob_moved(eg.lvl_mix[o], pz2)]) >> 15;
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
                e1_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][0], hold, eg.m_att[o][0], 0, ENV_FULL,
                                                     eg.m_rel[o][0]) * am1) >> 24);
                e2_to = (int32_t)(((int64_t)env_step(&voice[v].mod[o][1], hold, eg.m_att[o][1], 0, ENV_FULL,
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
            {                                   /* drive, the filter, the high-pass, the amp: the voice into both channels */
                int32_t fe = env_step(&voice[v].flt, hold, eg.f_att, eg.f_dec, eg.f_sus, eg.f_rel);
                int32_t f[3], f2[3], a1, a2, a3, d1, d2, d3, b1 = 0, b2 = 0, b3 = 0, e1 = 0, e2 = 0, e3 = 0;
                int32_t ic1 = voice[v].ic1, ic2 = voice[v].ic2, ic3 = voice[v].ic3, ic4 = voice[v].ic4, pk;
                int32_t drive = eg.drive, hg, hp1 = voice[v].hp1, hp2 = voice[v].hp2, c;
                int32_t fk = eg.f_k, hp_g = eg.hp_g, c_fb = eg.comb_fb, c_in = eg.comb_in, c_dk = eg.comb_dk;
                int32_t f_env = eg.f_env;
                int32_t cfb = 0, cin = 0, cdk = 0, cd = 0, dd = 0, clp = voice[v].clp, ap_a = 0;
                int32_t ax0 = 0, ax1 = 0, ax2 = 0, ax3 = 0, ay0 = 0, ay1 = 0, ay2 = 0, ay3 = 0;
                uint32_t cw = voice[v].cw, type = eg.flt_type, comb = type >= FLT_COMBP, lp24 = type == FLT_LP24, self;
                int16_t *cb = comb_buf[v];
                switch (eg.l2_dest) {           /* LFO 2 on the filter's knobs */
                case DEST_RES:
                    fk = PR_RES_K[knob_moved(eg.res, pz2)];
                    if (comb) comb_of(knob_moved(eg.res, pz2), type, &c_fb, &c_in);
                    break;
                case DEST_DRIVE: drive = drive_of(knob_moved(eg.drv, pz2)); break;
                case DEST_HPF: hp_g = hp_g_of(knob_moved(eg.hp, pz2)); break;
                case DEST_DAMP: c_dk = damp_of(knob_moved(eg.damp, pz2)); break;
                case DEST_FENV: f_env += (x2 * 240) >> 15; break;   /* +-60 semitones */
                default: break;
                }
                voice[v].f_cs = glide(voice[v].f_cs, eg.f_cut << 8);   /* knob moves glide, they do not step */
                voice[v].f_ks = glide(voice[v].f_ks, fk);
                self = voice[v].f_ks < K_SELF;  /* (the top of RES: past SELF_LVL the damping rises; at k < 0 the */
                if (!self)                      /*  filter grows into a sine and settles there, at any cutoff) */
                    voice[v].f_pk = 0;
                else if (voice[v].f_ks < 0 && !comb && ic1 > -4 && ic1 < 4 && ic2 > -4 && ic2 < 4)
                    ic1 = 2048;                 /* (nothing in it yet: a nudge, as an analog filter's noise; it grows */
                                                /*  from there to its level, down to the lowest cutoffs) */
                voice[v].hp_gs = hg = glide(voice[v].hp_gs, hp_g);
                c = cut_at(voice[v].note, fe, voice[v].f_cs + (m.cut << 8), f_env);
                if (comb) {                     /* the comb: the cutoff (keytracked, the envelope, the mods) -> a delay */
                    int32_t p16 = eg.comb_p16 + ((c - voice[v].f_cs) >> 6)   /* (its own CUT scale: in tune) */
                                + ((((voice[v].glide_q8 >> 8) + m.pitch16[0]) * eg.f_key) >> 8);
                    int32_t sp = eg.comb_spread + m.spread, d, per;
                    uint32_t inc;
                    p16 = p16 < 16 * 16 ? 16 * 16 : p16 > 127 * 16 ? 127 * 16 : p16;   /* (MIDI 16 .. 127) */
                    inc = rate_moved(INC_C9[(p16 >> 4) % 12u] >> (10u - (uint32_t)(p16 >> 4) / 12u), p16 & 15) >> 8;
                    per = inc ? (int32_t)(0xFFFFFFFFu / inc) : (int32_t)((COMB_N - 2u) << 8);   /* a period: 2^40 / inc */
                    d = per;
                    sp = sp < 0 ? 0 : sp > 127 ? 127 : sp;
                    if (sp) {                   /* SPREAD: four allpasses, their corner above f0 (60 .. 10 semitones) */
                        int32_t q0 = p16 >> 2, qc = q0 + 240 - (sp - 1) * 200 / 126, sh;
                        uint32_t t0, tc, x, fr;
                        if (qc > (int32_t)PR_FILT_N - 1) qc = (int32_t)PR_FILT_N - 1;
                        t0 = PR_FILT_G[q0];
                        tc = PR_FILT_G[qc];
                        ap_a = 16384 - (int32_t)((0xFFFFFFFFu / ((tc + (1u << 24)) >> 8)) >> 1);   /* (tc-1)/(tc+1), Q14 */
                        if (!ap_a) ap_a = 1;    /* (0: a plain frame's delay each, still to be run) */
                        sh = (int32_t)msb32(tc) - 15;   /* t0 / tc (<= 1), Q16, in 32 bits */
                        if (sh < 0) sh = 0;
                        x = ((t0 >> sh) << 16) / ((tc >> sh) | 1u);
                        x = x > 65536u ? 65536u : x;
                        fr = x >> 10;           /* (4 / pi) atan(x): how much of a period they delay f0 by */
                        x = PR_ATAN4_Q15[fr] + (((PR_ATAN4_Q15[fr < 64u ? fr + 1u : 64u] - PR_ATAN4_Q15[fr]) * (x & 1023u)) >> 10);
                        d = per - (int32_t)((((uint32_t)per >> 4) * x) >> 11);   /* the line: the rest of the period */
                    }
                    if (d < (int32_t)(2u << 8)) d = 2 << 8;
                    if (d > (int32_t)((COMB_N - 2u) << 8)) d = (int32_t)((COMB_N - 2u) << 8);
                    if (!voice[v].comb_live) {  /* (turned on, or a new note from silence: no old sound in the line) */
                        memset(cb, 0, sizeof comb_buf[0]);
                        memset(voice[v].apx, 0, sizeof voice[v].apx);
                        memset(voice[v].apy, 0, sizeof voice[v].apy);
                        clp = 0;
                        voice[v].cd_q8 = d;
                        voice[v].comb_live = 1;
                    }
                    cd = voice[v].cd_q8;
                    dd = (d - cd) >> BLK_SHIFT; /* the delay glides across the block */
                    voice[v].cd_q8 = d;
                    voice[v].cfb = cfb = glide(voice[v].cfb, c_fb);
                    voice[v].cdk = cdk = glide(voice[v].cdk, c_dk);
                    voice[v].cin = cin = glide(voice[v].cin, c_in);
                    voice[v].fc[0] = -1;        /* (the low-pass, back on, starts on its own block) */
                    a1 = a2 = a3 = d1 = d2 = d3 = 0;
                } else {
                    voice[v].comb_live = 0;
                    {
                        int32_t k = voice[v].f_ks, over = voice[v].f_pk - SELF_LVL;
                        if (self && over > 0 && k < K_HELD) {   /* 0.1 more damping each SELF_LVL over (Q24), up */
                            k += over > SELF_LVL ? 1677722 : over * 168;   /* to 0.02: it rings on, never dies */
                            if (k > K_HELD) k = K_HELD;
                        }
                        voice[v].f_pk -= voice[v].f_pk >> 6;   /* (falls in ~23 ms) */
                        filter_coefs(c, k, f, lp24 ? f2 : 0);
                    }
                    if (voice[v].fc[0] < 0) {   /* (a voice from silence: no previous block to glide from) */
                        voice[v].fc[0] = f[0]; voice[v].fc[1] = f[1]; voice[v].fc[2] = f[2];
                        voice[v].fc2[0] = -1;
                    }
                    a1 = voice[v].fc[0]; a2 = voice[v].fc[1]; a3 = voice[v].fc[2];
                    d1 = (f[0] - a1) >> BLK_SHIFT;   /* the coefficients glide across the block, frame by frame */
                    d2 = (f[1] - a2) >> BLK_SHIFT;
                    d3 = (f[2] - a3) >> BLK_SHIFT;
                    voice[v].fc[0] = f[0]; voice[v].fc[1] = f[1]; voice[v].fc[2] = f[2];
                    if (lp24) {
                        if (voice[v].fc2[0] < 0) {
                            voice[v].fc2[0] = f2[0]; voice[v].fc2[1] = f2[1]; voice[v].fc2[2] = f2[2];
                            ic3 = ic4 = 0;
                        }
                        b1 = voice[v].fc2[0]; b2 = voice[v].fc2[1]; b3 = voice[v].fc2[2];
                        e1 = (f2[0] - b1) >> BLK_SHIFT;
                        e2 = (f2[1] - b2) >> BLK_SHIFT;
                        e3 = (f2[2] - b3) >> BLK_SHIFT;
                        voice[v].fc2[0] = f2[0]; voice[v].fc2[1] = f2[1]; voice[v].fc2[2] = f2[2];
                    } else
                        voice[v].fc2[0] = -1;
                }
                pk = voice[v].f_pk;
                if (ap_a) {
                    ax0 = voice[v].apx[0]; ax1 = voice[v].apx[1]; ax2 = voice[v].apx[2]; ax3 = voice[v].apx[3];
                    ay0 = voice[v].apy[0]; ay1 = voice[v].apy[1]; ay2 = voice[v].apy[2]; ay3 = voice[v].apy[3];
                }
                for (i = 0; i < BLK; i++, a += da) {
                    int32_t s = acc[i] > 32767 ? 32767 : acc[i] < -32767 ? -32767 : acc[i], v1, v2, v3;
                    if (drive)
                        s = saturate((s * drive) >> 12);
                    if (comb) {                 /* the comb: the line read a period back (interpolated), damped, fed back */
                        uint32_t pos = (cw << 8) - (uint32_t)cd, ri = (pos >> 8) & COMB_MASK;
                        int32_t c0 = cb[ri], u;
                        u = c0 + (((cb[(ri + 1u) & COMB_MASK] - c0) * (int32_t)(pos & 255u)) >> 8);
                        if (ap_a) {             /* SPREAD: y = a (x - y1) + x1, four times (in registers) */
#define AP(x, y) do { int32_t t = (((u - y) * ap_a) >> 14) + x; x = u; \
                      y = u = t > 32767 ? 32767 : t < -32767 ? -32767 : t; } while (0)
                            AP(ax0, ay0); AP(ax1, ay1); AP(ax2, ay2); AP(ax3, ay3);
#undef AP
                        }
                        clp += ((u - clp) * cdk) >> 15;
                        s = saturate(((s * cin) >> 15) + ((clp * cfb) >> 15));   /* (loud peaks round off, not clip) */
                        cb[cw] = (int16_t)s;
                        cw = (cw + 1u) & COMB_MASK;
                        cd += dd;
                    } else {
                        if (lp24) {             /* LP 24: a flat (Butterworth) stage first */
                            v3 = s - ic4;
                            v1 = (int32_t)(((int64_t)b1 * ic3 + (int64_t)b2 * v3) >> 28);
                            v2 = ic4 + (int32_t)(((int64_t)b2 * ic3 + (int64_t)b3 * v3) >> 28);
                            ic3 = 2 * v1 - ic3;
                            ic4 = 2 * v2 - ic4;
                            s = v2 > 32767 ? 32767 : v2 < -32767 ? -32767 : v2;
                            b1 += e1; b2 += e2; b3 += e3;
                        }
                        v3 = s - ic2;           /* the zero-delay-feedback SVF (Simper): stable at any cutoff */
                        v1 = (int32_t)(((int64_t)a1 * ic1 + (int64_t)a2 * v3) >> 28);
                        v2 = ic2 + (int32_t)(((int64_t)a2 * ic1 + (int64_t)a3 * v3) >> 28);
                        ic1 = 2 * v1 - ic1;
                        ic2 = 2 * v2 - ic2;
                        if (self) {             /* the band state's peak, for the next block's damping */
                            int32_t m = ic1 < 0 ? -ic1 : ic1;
                            if (m > pk) pk = m;
                        }
                        s = v2 > 32767 ? 32767 : v2 < -32767 ? -32767 : v2;   /* the low-pass */
                        a1 += d1; a2 += d2; a3 += d3;
                    }
                    if (hg) {                   /* the high-pass: two one-poles (TPT), what each lets through low gone */
                        int32_t u = ((s - hp1) * hg) >> 15, lp = u + hp1;
                        hp1 = lp + u;
                        s -= lp;
                        u = ((s - hp2) * hg) >> 15;
                        lp = u + hp2;
                        hp2 = lp + u;
                        s -= lp;
                        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;
                    }
                    s = (((s * a) >> 15) * VOICE_GAIN) >> 15;
                    out[2u * (b + i)] += s;
                    out[2u * (b + i) + 1u] += s;
                }
                voice[v].ic1 = comb ? 0 : ic1;
                voice[v].f_pk = pk;
                voice[v].ic2 = comb ? 0 : ic2;
                voice[v].ic3 = lp24 ? ic3 : 0;
                voice[v].ic4 = lp24 ? ic4 : 0;
                voice[v].hp1 = hg ? hp1 : 0;
                voice[v].hp2 = hg ? hp2 : 0;
                voice[v].clp = clp;
                if (ap_a) {
                    voice[v].apx[0] = ax0; voice[v].apx[1] = ax1; voice[v].apx[2] = ax2; voice[v].apx[3] = ax3;
                    voice[v].apy[0] = ay0; voice[v].apy[1] = ay1; voice[v].apy[2] = ay2; voice[v].apy[3] = ay3;
                }
                voice[v].cw = (uint16_t)cw;
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
    fx_render(out, n, g2);                      /* the effects, over all the voices (LFO 2 on them) */
    click_render(out, n);                       /* the metronome, dry */
    engine_frames += n;
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

static void voice_tune(uint32_t v)              /* the operators' phase steps for the voice's note */
{
    uint32_t base = INC_C9[voice[v].note % 12u] >> (10u - voice[v].note / 12u), o, k;
    for (o = 0; o < NOSC; o++) {
        uint64_t f = ((uint64_t)base * pitch_q16(o)) >> 16;
        for (k = 0; k < NOP; k++) {
            uint64_t inc;
            if (k && patch.osc[o].mode[k - 1u] != MODE_FM) {   /* a modulator heard (MIX, RING): its pitch */
                uint32_t sm = patch.mpitch[o][k - 1u];
                inc = (f * PR_SEMI_Q16[sm <= 48u ? sm : 24u]) >> 16;
            } else
                inc = (f * PR_RATIO_Q8[patch.osc[o].ratio[k] < PR_RATIOS ? patch.osc[o].ratio[k] : 1u]) >> 8;
            if (k)
                inc = (inc * PR_FINE_Q16[patch.mfine[o][k - 1u] & 127u]) >> 16;
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
        eg.lvl_osc[o] = PR_CURVE_Q15[patch.osc[o].level & 127u];
        eg.lvl_mix[o] = (o ? patch.lvl2 : patch.lvl1) & 127u;
        eg.lvl[o] = (eg.lvl_osc[o] * PR_CURVE_Q15[eg.lvl_mix[o]]) >> 15;
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
    eg.drive = drive_of(patch.drive);
    eg.hp_g = hp_g_of(patch.hp_cut);
    eg.res = patch.res & 127u;
    eg.drv = patch.drive & 127u;
    eg.hp = patch.hp_cut & 127u;
    eg.damp = patch.comb_damp & 127u;
    eg.flt_type = patch.flt_type < FLT_N ? patch.flt_type : FLT_LP12;
    {
        int32_t fb, in;
        comb_of(patch.res, eg.flt_type, &fb, &in);
        eg.comb_fb = fb;
        eg.comb_in = in;
    }
    eg.comb_p16 = 12 * 16 + (int32_t)(patch.cut & 127u) * 12;
    eg.comb_spread = patch.comb_spread & 127u;
    eg.comb_dk = damp_of(patch.comb_damp);
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
        eg.lfo_dest[k] = DEST_LFO_OK(patch.lfo_dest[k]) ? patch.lfo_dest[k] : DEST_OFF;   /* (not itself) */
        eg.lfo_amt[k] = eg.lfo_dest[k] == DEST_OFF ? 0 : d < 0 ? -q : q;
    }
    {                                           /* LFO 2 */
        int32_t d = (int32_t)(patch.lfo2_amt & 127u) - 64, mag = d < 0 ? -d : d, q = PR_CURVE_Q15[mag * 127 / 64];
        eg.l2_wave = patch.lfo2_wave < LFO_N ? patch.lfo2_wave : LFO_SINE;
        eg.l2_inc = PR_LFO_INC[patch.lfo2_rate & 127u];
        eg.l2_sync = patch.lfo2_sync < L2SYNC_N ? patch.lfo2_sync : L2SYNC_OFF;
        eg.l2_dest = DEST_LFO2_OK(patch.lfo2_dest) ? patch.lfo2_dest : DEST_OFF;
        eg.l2_amt = eg.l2_dest == DEST_OFF ? 0 : d < 0 ? -q : q;
    }
    for (k = 0; k < 2u; k++) {                  /* the mod envelopes */
        const uint8_t *e = k ? &patch.menv2_a : &patch.menv_a;   /* (a d s r loop dest amt, in a row in both) */
        int32_t d = (int32_t)(e[6] & 127u) - 64, mag = d < 0 ? -d : d, q = PR_CURVE_Q15[mag * 127 / 64];
        eg.me_att[k] = PR_ENV_ATT[e[0] & 127u];
        eg.me_dec[k] = PR_ENV_EXP[e[1] & 127u];
        eg.me_sus[k] = (int32_t)((e[2] & 127u) * (uint32_t)(ENV_FULL / 127));
        eg.me_rel[k] = PR_ENV_EXP[e[3] & 127u];
        eg.me_loop[k] = e[4] ? 1u : 0u;
        eg.me_dest[k] = e[5] < DEST_N ? e[5] : DEST_OFF;
        eg.me_amt[k] = eg.me_dest[k] == DEST_OFF ? 0 : d < 0 ? -q : q;
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
        eg.arp_spb = spb;
        eg.tick_q8 = 44100u * 60u * 256u / (bpm * 24u);
        eg.arp_mode = patch.arp_mode < ARP_N ? patch.arp_mode : ARP_UP;
        eg.arp_oct = patch.arp_oct <= 3u ? patch.arp_oct : 0u;
        eg.arp_latch = patch.arp_latch ? 1u : 0u;
        if (!eg.arp_latch && held_down == 0u && held.n) arp_clear();   /* latch off: the latched notes go */
        eg.arp_tie = (patch.arp_gate & 127u) == 127u;
        eg.arp_gate = 1638 + (int32_t)(patch.arp_gate & 127u) * (32768 - 1638) / 127;   /* 5 .. 100 % */
        eg.arp_swing = (int32_t)(patch.arp_swing & 127u) * 16384 / 127;
        eg.arp_on = patch.arp_on ? 1u : 0u;
        spb = DIV_SPB[seqs.div < DIV_N ? seqs.div : DIV_16];   /* the sequencer: the arp's tempo, its step */
        eg.seq_len_q8 = 44100u * 60u * 256u / (bpm * spb);
        eg.seq_spb = spb;
        eg.seq_gate = 1638 + (int32_t)(seqs.gate & 127u) * (32768 - 1638) / 127;
    }
    fx_set(&patch);
    eg.a_att = PR_ENV_ATT[patch.amp_a & 127u];
    eg.a_dec = PR_ENV_EXP[patch.amp_d & 127u];
    eg.a_sus = (int32_t)((patch.amp_s & 127u) * (uint32_t)(ENV_FULL / 127));
    eg.a_rel = PR_ENV_EXP[patch.amp_r & 127u];
    {                                           /* VCA OPEN */
        int32_t vca = (int32_t)(patch.vca & 127u) * 32767 / 127;
        if (vca && !eg.vca) {                   /* opened: the oscillators drone where they were (if nothing sounds) */
            uint32_t w, busy = 0;
            for (w = 0; w < NV; w++) {
                busy |= voice[w].amp.st || voice[w].gate || voice[w].trig;
                if (!voice[w].amp.st && !voice[w].gate) voice[w].drone = 0;   /* (silent ones: not their old notes) */
            }
            if (!busy) {                        /* a note started without a key: its envelopes run, its amp at the floor */
                voice[0].glide_q8 = 0;
                voice[0].note = (uint8_t)(last_note >= 0 ? last_note : 48);   /* (the last note; at power-on C3) */
                voice[0].vel = 127;
                voice[0].age = ++ages;
                voice_tune(0);
                voice[0].gate = 0;
                voice[0].trig = 1;              /* (the ISR starts it: voice_start) */
            }
        }
        eg.vca = vca;
    }
    for (v = 0; v < NV; v++)                    /* ratios and tuning may have changed: retune what sounds */
        if (voice[v].gate || voice_on(v))
            voice_tune(v);
}

static void engine_init(void)
{
    uint32_t o, k, v;
    fx_init();
    seqs_init();                                /* (patches.c then brings the saved ones) */
    l2_ph = 0;                                  /* (LFO 2 from the top: the same every time) */
    for (v = 0; v < NV; v++)
        voice[v].itrig = voice[v].ioff = voice[v].islide = 0xFF;
    for (o = 0; o < NOSC; o++)                  /* force every table to be built */
        for (k = 0; k < NOP; k++)
            op_cur[o][k] = 0;
    engine_set(&PATCH_INIT);
    engine_up = 1;
}

/* ---- notes (main loop) */
static void engine_note_on_vel(uint32_t note, uint32_t vel)   /* vel 1 .. 127 */
{
    uint32_t pick;
    if (note > 127u)
        return;
    pick = voice_pick(note);
    voice[pick].glide_q8 = glide_from(note);
    voice[pick].gk = eg.glide_k;
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

static int engine_quiet(void)                   /* nothing sounds, not even a release; no arp or sequence playing */
{
    uint32_t v;
    if ((eg.arp_on && held.n) || eg.seq_play)
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

static void engine_bend(uint32_t v14, uint32_t range)   /* MIDI pitch bend 0..16383 (8192: none), +-range st */
{
    int32_t d = (int32_t)v14 - 8192;
    bend16 = d * (int32_t)range * 16 / (d > 0 ? 8191 : 8192);   /* (up: 8191 is the top) */
}

static void engine_seq_play(uint32_t on) { eg.seq_play = on ? 1u : 0u; }   /* the sequencer: run / stop (PLAY) */
static void engine_seq_countin(void) { seq.want_ci = 1; }   /* the next PLAY: a bar's count-in first */
static void engine_click(uint32_t mode, uint32_t lvl, uint32_t rec)   /* the metronome: OFF, ON, REC; its level; recording */
{
    clk_set.mode = (uint8_t)mode;
    clk_set.lvl = (uint8_t)lvl;
    clk_set.rec = (uint8_t)rec;
}
static void engine_seq_mute(uint32_t lanes) { seq.rec_mute = (uint8_t)lanes; }   /* lanes that rest (recording) */

/* the step a note played now falls on (recording): the one playing, or the next once past its middle; and its count
 * (seq.steps: the one playing is steps - 1). 0: the sequencer is not playing */
static int engine_seq_now(seqpos_t *at, uint32_t *ord)
{
    uint32_t n, len;
    int32_t t;
    seqpos_t cur, nx;
    do {                                        /* (a copy no step was half-way through) */
        n = seq.steps;
        cur = seq.now_at;
        nx = seq.at;
        t = (int32_t)(engine_frames - seq.step_at);   /* (under 0: the step starts later in this render) */
        len = seq.step_len;
    } while (n != seq.steps);
    if (seq.in_ci) {                            /* counting in: only late in its last step, for the first */
        if (seq.ci || t <= 0 || ((uint64_t)t << 9) <= len)
            return 0;
        *at = nx;
        *ord = n;
        return 1;
    }
    if (!eg.seq_play || !seq.was || !n)
        return 0;
    if (t > 0 && ((uint64_t)t << 9) > len) { *at = nx; *ord = n; }   /* past half the step (len: frames Q8) */
    else { *at = cur; *ord = n - 1u; }
    return 1;
}

static void engine_sync(uint32_t midi)          /* the arp's clock: its own (0) or the MIDI clock (1) */
{
    if (eg.arp_midi != (midi ? 1u : 0u)) {
        mc.running = 0;                         /* (the MIDI clock waits for START or CONTINUE) */
        mc.wait = -1;
        arp.n_was = 0;
    }
    eg.arp_midi = midi ? 1u : 0u;
}

static void engine_all_off(void)                /* every note let go (the arp switched on or off) */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        voice[v].gate = 0;
}
