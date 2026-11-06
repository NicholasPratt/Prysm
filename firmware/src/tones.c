/* SPDX-License-Identifier: GPL-3.0-only */
/* Tones: a part's sound on its own (D-50 style), apart from the patch and its sequencer: the oscillator (its waves,
 * modulators, their pitches), its level, transient and route, and the part's own fields (PART_OFF: the filter, the
 * envelopes, the LFO, M.ENV 1, velocity, glide, the noise). Loaded into part A or part B of the sound edited; the rest
 * of the patch (the other part, the effects, LFO 2, M.ENV 2, the matrix, the arp) stays as it is.
 *
 * 96 factory tones (TONE_ROM: each a few changes from the default sound's part A, made into a tone when picked) and
 * 64 user tones, in two halves of 32 (one in RAM at a time; the seqstore's areas TONE_AREA, TONE_AREA + 1: platform.c, blobs 0 .. 3). A
 * tone's destinations are its own oscillator's (WAVE1, MWAVE1, MOD1, M1PITCH): in part B they are osc 2's (engine.c:
 * dest_b), so a tone sounds the same in either part. Included by prysm.c after patches.c */
#define TONE_NAME 10u                           /* (9 characters and the NUL) */
#define TONE_ROM_N 96u
#define TONE_USER_N 64u
#define TONE_HALF 32u                           /* (user tones a store area) */
#define TONE_AREA (SEQSTORE_N - 2u)             /* (the seqstore's areas of the two halves) */
#define TONE_VERSION 1u
typedef __typeof__(((patch_t *)0)->osc[0]) osc_t;
typedef struct {
    char name[TONE_NAME];                       /* (a user tone: name[0] 0 = empty) */
    osc_t osc;
    uint8_t mpitch[2], mfine[2];                /* (the modulators' pitch in MIX / RING, their fine) */
    uint8_t lvl;                                /* the oscillator's level (MIX page) */
    uint8_t semi, fine;                         /* (osc 2's SEMI, FINE: part B's; part A's 24, 64) */
    uint8_t route;                              /* FILTER / DIRECT */
    uint8_t trn[4];                             /* the transient: type, level, tune, decay */
    uint8_t pf[PART_N];                         /* the part's own fields, in PART_OFF's order */
} tone_t;
typedef struct { uint32_t version; tone_t t[TONE_HALF]; } tone_half_t;
static tone_half_t tone_user;                   /* (one half in RAM: the one last looked at) */
static uint8_t tone_user_h = 0xFFu;             /* (which: 0, 1; 0xFF none yet) */

static uint32_t dest_a(uint32_t d)              /* (part B's osc-2 destinations as a tone has them: its own osc's) */
{
    return d == DEST_WAVE2 ? DEST_WAVE1 : d == DEST_MWAVE2 ? DEST_MWAVE1 : d == DEST_MOD2 ? DEST_MOD1 :
           d == DEST_M2PITCH ? DEST_M1PITCH : d;
}
static int part_dest_field(uint32_t i)         /* PART_OFF[i] is a destination (the LFO's two, M.ENV 1's) */
{
    uint32_t o = PART_OFF[i];
    return o == __builtin_offsetof(patch_t, lfo_dest[0]) || o == __builtin_offsetof(patch_t, lfo_dest[1]) ||
           o == __builtin_offsetof(patch_t, menv_dest);
}

static void tone_get(const patch_t *p, uint32_t part, tone_t *t)   /* part (0 A, 1 B) of p as a tone (its name kept) */
{
    uint32_t o = part & 1u, i, k;
    t->osc = p->osc[o];
    for (k = 0; k < 2u; k++) { t->mpitch[k] = p->mpitch[o][k]; t->mfine[k] = p->mfine[o][k]; }
    t->lvl = o ? p->lvl2 : p->lvl1;
    t->semi = o ? p->semi2 : 24u;
    t->fine = o ? p->fine2 : 64u;
    t->route = p->route[o];
    t->trn[0] = p->trn_type[o]; t->trn[1] = p->trn_lvl[o]; t->trn[2] = p->trn_tune[o]; t->trn[3] = p->trn_dec[o];
    for (i = 0; i < PART_N; i++) {
        uint32_t v = o ? p->pb[i] : ((const uint8_t *)p)[PART_OFF[i]];
        t->pf[i] = (uint8_t)(o && part_dest_field(i) ? dest_a(v) : v);
    }
}
static void tone_put(patch_t *p, uint32_t part, const tone_t *t)   /* a tone into part (0 A, 1 B) of p */
{
    uint32_t o = part & 1u, i, k;
    p->osc[o] = t->osc;
    for (k = 0; k < 2u; k++) { p->mpitch[o][k] = t->mpitch[k]; p->mfine[o][k] = t->mfine[k]; }
    if (o) { p->lvl2 = t->lvl; p->semi2 = t->semi; p->fine2 = t->fine; } else p->lvl1 = t->lvl;
    p->route[o] = t->route;
    p->trn_type[o] = t->trn[0]; p->trn_lvl[o] = t->trn[1]; p->trn_tune[o] = t->trn[2]; p->trn_dec[o] = t->trn[3];
    for (i = 0; i < PART_N; i++) {
        if (o) p->pb[i] = t->pf[i];
        else ((uint8_t *)p)[PART_OFF[i]] = t->pf[i];
    }
}

/* ---- the factory tones: each its changes to the default sound's part A, (offset in patch_t, value) pairs to TE */
#define TP(f) ((uint8_t)__builtin_offsetof(patch_t, f))
#define TO(f) TP(osc[0].f)
#define TE 0xFFu                                /* (the end: past part A's fields) */
/* waves (a bank's eight, on its knob), ratios (PR_RATIO_NAME's places), the transients (TR_NAME's) */
enum { W1 = 0, W2 = 18, W3 = 36, W4 = 54, W5 = 73, W6 = 91, W7 = 109, W8 = 127 };
enum { BK_BASIC, BK_HARM, BK_PULSE, BK_VOCAL, BK_DIGI, BK_CMPLX };
enum { R05, R1, R1H, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16 };
enum { T_HAMMER = 1, T_PLUCK, T_PICK, T_MALLET, T_KALIMB, T_BELL, T_GLASS, T_METAL, T_ANVIL, T_CHIFF, T_BREATH,
       T_FLUTE, T_BOW, T_BRASS, T_SLAP, T_KNOCK, T_CLICK, T_TICK, T_THUMP, T_ZAP, T_BLIP, T_NOISE, T_SPIT, T_VOX };
_Static_assert(T_VOX == TR_N, "the transients' names: as prysm_trans.h has them");
/* the common ones */
#define PURE TO(amt[0]), 0                      /* (the carrier alone: no FM) */
#define WAVE(b, w) TO(bank[0]), b, TO(pos[0]), w
#define DETUNED(f) TO(mode[0]), MODE_MIX, TO(bank[1]), BK_BASIC, TO(pos[1]), W5, TP(mpitch[0][0]), 24, \
                   TP(mfine[0][0]), f, TO(amt[0]), 110   /* (mod 1 a saw beside it, detuned) */
#define AMP(a, d, s, r) TP(amp_a), a, TP(amp_d), d, TP(amp_s), s, TP(amp_r), r
#define FLT(a, d, s, r) TP(flt_a), a, TP(flt_d), d, TP(flt_s), s, TP(flt_r), r
#define TRN(t, l) TP(trn_type[0]), t, TP(trn_lvl[0]), l
#define VIB(d) TP(lfo_dest[0]), DEST_PITCH, TP(lfo_amt[0]), 70, TP(lfo_rate), 84, TP(lfo_delay), d
typedef struct { const char *name; const uint8_t *kv; } tone_rom_t;
#define TONE(n, ...) {n, (const uint8_t[]){__VA_ARGS__, TE}}
static const tone_rom_t TONE_ROM[TONE_ROM_N] = {
    /* basses */
    TONE("SUB BASS", PURE, TP(cut), 60, TP(res), 20, TP(fenv), 70, TP(key), 40, FLT(0, 50, 40, 40), AMP(0, 60, 110, 30),
         TRN(T_THUMP, 70)),
    TONE("FM BASS", TO(ratio[1]), R1, TO(amt[0]), 90, TP(cut), 70, TP(res), 30, TP(fenv), 100, FLT(0, 55, 20, 40),
         AMP(0, 70, 90, 25), TRN(T_PICK, 60)),
    TONE("ACID", PURE, WAVE(BK_BASIC, W5), TP(flt_type), FLT_LP24, TP(cut), 40, TP(res), 100, TP(fenv), 105,
         TP(drive), 50, TP(key), 40, FLT(0, 50, 0, 40), AMP(0, 80, 100, 20), TP(glide), 40, TP(glide_mode), GLIDE_LEGATO),
    TONE("REESE", WAVE(BK_BASIC, W5), DETUNED(76), TP(flt_type), FLT_LP24, TP(cut), 55, TP(res), 30, TP(fenv), 72,
         TP(drive), 40, TP(lfo_amt[1]), 74, TP(lfo_rate), 40, TP(lfo_retrig), 0, AMP(0, 70, 120, 30)),
    TONE("PLUCKBASS", PURE, WAVE(BK_BASIC, W6), TP(flt_type), FLT_LP24, TP(cut), 45, TP(res), 50, TP(fenv), 110,
         FLT(0, 40, 0, 40), AMP(0, 55, 0, 40), TRN(T_PLUCK, 60)),
    TONE("WOOD BASS", TO(ratio[1]), R1, TO(amt[0]), 55, TP(cut), 75, TP(fenv), 85, FLT(0, 55, 10, 40),
         AMP(0, 75, 0, 50), TRN(T_KNOCK, 80)),
    TONE("DIGI BASS", PURE, WAVE(BK_DIGI, 40), TP(cut), 80, TP(res), 20, TP(fenv), 80, TP(drive), 30,
         FLT(0, 60, 30, 40), AMP(0, 70, 110, 25)),
    TONE("SYNC BASS", PURE, WAVE(BK_CMPLX, 60), TP(menv_dest), DEST_WAVE1, TP(menv_amt), 100, TP(menv_a), 0,
         TP(menv_d), 55, TP(menv_s), 0, TP(cut), 85, TP(fenv), 80, FLT(0, 60, 40, 40), AMP(0, 70, 110, 25)),
    /* keys */
    TONE("E.PIANO", TO(ratio[1]), R1, TO(amt[0]), 45, TP(cut), 100, TP(fenv), 70, FLT(0, 70, 40, 60),
         AMP(0, 85, 30, 60), TRN(T_MALLET, 70), TP(trn_dec[0]), 70, TP(vel_mod), 90),
    TONE("CLAV", PURE, WAVE(BK_PULSE, 70), TP(cut), 85, TP(res), 50, TP(fenv), 95, TP(hp_cut), 30,
         FLT(0, 45, 20, 30), AMP(0, 60, 20, 25), TRN(T_PICK, 90)),
    TONE("ORGAN", PURE, WAVE(BK_HARM, W4), TP(cut), 110, TP(fenv), 64, AMP(2, 70, 127, 15), TRN(T_CLICK, 60), VIB(0)),
    TONE("PERC ORG", PURE, WAVE(BK_HARM, W3), TP(cut), 115, TP(fenv), 64, AMP(0, 70, 127, 10), TRN(T_TICK, 80)),
    TONE("TOY PIANO", TO(ratio[1]), R5, TO(amt[0]), 45, TP(cut), 110, AMP(0, 60, 0, 50), TRN(T_KALIMB, 100)),
    TONE("HARPSI", PURE, WAVE(BK_PULSE, 100), TP(cut), 105, TP(fenv), 80, TP(hp_cut), 20, FLT(0, 60, 20, 30),
         AMP(0, 70, 0, 30), TRN(T_PLUCK, 110), TP(trn_tune[0]), 36),
    TONE("WURLY", TO(ratio[1]), R1, TO(amt[0]), 60, TP(drive), 40, AMP(0, 80, 40, 50), TRN(T_MALLET, 60),
         TP(lfo_dest[0]), DEST_AMP, TP(lfo_amt[0]), 74, TP(lfo_rate), 85, TP(lfo_retrig), 0),
    TONE("GLASSKEYS", TO(ratio[1]), R3, TO(amt[0]), 50, TP(cut), 105, AMP(0, 80, 20, 70), TRN(T_GLASS, 90)),
    /* pads */
    TONE("WARM PAD", WAVE(BK_BASIC, W5), DETUNED(72), TP(cut), 60, TP(res), 20, TP(fenv), 70, FLT(80, 90, 60, 80),
         AMP(85, 90, 110, 90), TP(lfo_amt[1]), 72, TP(lfo_rate), 40, TP(lfo_retrig), 0),
    TONE("STRINGS", WAVE(BK_BASIC, W5), DETUNED(75), TP(cut), 75, TP(res), 10, TP(fenv), 66, AMP(70, 80, 115, 80),
         VIB(70), TRN(T_BOW, 50)),
    TONE("CHOIR", WAVE(BK_VOCAL, W3), TO(mode[0]), MODE_MIX, TO(bank[1]), BK_VOCAL, TO(pos[1]), W2,
         TP(mpitch[0][0]), 24, TP(mfine[0][0]), 70, TO(amt[0]), 80, TP(cut), 85, AMP(80, 80, 110, 85), VIB(40)),
    TONE("GLASS PAD", TO(ratio[1]), R3, TO(amt[0]), 55, TO(att[0]), 90, TP(cut), 110, AMP(75, 80, 110, 95),
         TP(lfo_dest[1]), DEST_MOD1, TP(lfo_amt[1]), 74, TP(lfo_rate), 50, TP(lfo_retrig), 0),
    TONE("SWEEP PAD", WAVE(BK_BASIC, W5), DETUNED(70), TP(flt_type), FLT_LP24, TP(cut), 40, TP(res), 70, TP(fenv), 64,
         TP(lfo_wave), LFO_TRI, TP(lfo_amt[1]), 100, TP(lfo_rate), 30, TP(lfo_retrig), 0, AMP(80, 80, 110, 90)),
    TONE("HOLLOW", PURE, WAVE(BK_PULSE, 40), TP(flt_type), FLT_COMBM, TP(cut), 70, TP(res), 90, TP(comb_damp), 40,
         TP(fenv), 64, TP(lfo_amt[1]), 70, TP(lfo_rate), 35, TP(lfo_retrig), 0, AMP(70, 80, 110, 85)),
    TONE("AIR PAD", PURE, TP(noise_type), NOISE_PINK, TP(noise_lvl), 60, TP(noise_col), 90, TP(cut), 80,
         TP(fenv), 64, TP(hp_cut), 30, AMP(90, 80, 110, 95)),
    TONE("DARKDRONE", WAVE(BK_CMPLX, 30), TO(ratio[1]), R05, TO(amt[0]), 50, TP(flt_type), FLT_LP24, TP(cut), 45,
         TP(res), 60, TP(fenv), 64, AMP(95, 80, 110, 100), TP(lfo_wave), LFO_TRI, TP(lfo_dest[1]), DEST_WAVE1,
         TP(lfo_amt[1]), 84, TP(lfo_rate), 25, TP(lfo_retrig), 0),
    /* leads */
    TONE("SAW LEAD", WAVE(BK_BASIC, W5), DETUNED(72), TP(cut), 100, TP(res), 30, TP(fenv), 75, AMP(2, 70, 120, 35),
         TP(glide), 30, TP(glide_mode), GLIDE_LEGATO, VIB(60)),
    TONE("SQUARE LD", PURE, WAVE(BK_BASIC, W6), TP(cut), 95, TP(fenv), 70, AMP(2, 70, 120, 30), VIB(70)),
    TONE("SYNC LEAD", PURE, WAVE(BK_CMPLX, 70), TP(menv_dest), DEST_WAVE1, TP(menv_amt), 95, TP(menv_a), 0,
         TP(menv_d), 70, TP(menv_s), 0, TP(cut), 105, TP(res), 20, AMP(2, 70, 120, 30)),
    TONE("FM LEAD", TO(ratio[1]), R1, TO(amt[0]), 85, TO(ratio[2]), R2, TO(amt[1]), 40, AMP(2, 70, 120, 30),
         TP(vel_mod), 90, VIB(60)),
    TONE("FLUTE", TO(ratio[1]), R1, TO(amt[0]), 15, TP(noise_type), NOISE_PINK, TP(noise_lvl), 25, TP(cut), 90,
         AMP(30, 70, 120, 40), TRN(T_FLUTE, 70), VIB(80)),
    TONE("WHISTLE", PURE, TP(cut), 110, AMP(25, 70, 120, 35), TRN(T_CHIFF, 40), TP(glide), 25,
         TP(lfo_dest[0]), DEST_PITCH, TP(lfo_amt[0]), 71, TP(lfo_rate), 90, TP(lfo_delay), 50),
    TONE("BRASS LD", WAVE(BK_BASIC, W5), DETUNED(70), TP(cut), 70, TP(fenv), 95, FLT(45, 70, 70, 40),
         AMP(25, 70, 120, 35), TRN(T_BRASS, 60)),
    TONE("VOX LEAD", PURE, WAVE(BK_VOCAL, W5), TP(menv_dest), DEST_WAVE1, TP(menv_amt), 90, TP(menv_a), 60,
         TP(menv_s), 60, AMP(10, 70, 120, 40), VIB(60)),
    /* bells, mallets */
    TONE("DX BELL", TO(ratio[1]), R3, TO(amt[0]), 80, TP(mfine[0][0]), 80, TP(cut), 120, AMP(0, 100, 0, 95),
         TRN(T_BELL, 70)),
    TONE("TUBULAR", TO(ratio[1]), R1H, TO(amt[0]), 60, TP(cut), 115, AMP(0, 105, 0, 100), TRN(T_ANVIL, 50)),
    TONE("MARIMBA", TO(ratio[1]), R4, TO(amt[0]), 35, TP(cut), 110, AMP(0, 60, 0, 50), TRN(T_MALLET, 110)),
    TONE("VIBES", TO(ratio[1]), R4, TO(amt[0]), 30, TP(cut), 115, AMP(0, 90, 0, 85), TRN(T_MALLET, 80),
         TP(lfo_dest[0]), DEST_AMP, TP(lfo_amt[0]), 74, TP(lfo_rate), 80, TP(lfo_retrig), 0),
    TONE("KALIMBA", TO(ratio[1]), R5, TO(amt[0]), 35, TP(cut), 110, AMP(0, 70, 0, 60), TRN(T_KALIMB, 120)),
    TONE("GLOCK", TO(ratio[1]), R8, TO(amt[0]), 40, TP(cut), 120, AMP(0, 85, 0, 80), TRN(T_GLASS, 100),
         TP(trn_tune[0]), 36),
    TONE("STEELDRUM", TO(ratio[1]), R1H, TO(amt[0]), 50, TP(cut), 100, TP(fenv), 80, FLT(0, 50, 30, 50),
         AMP(0, 75, 0, 60), TRN(T_METAL, 70)),
    TONE("RING BELL", TO(mode[0]), MODE_RING, TP(mpitch[0][0]), 31, TO(amt[0]), 110, TP(cut), 115,
         AMP(0, 100, 0, 95), TRN(T_BELL, 50)),
    /* plucks */
    TONE("PLUCK", PURE, WAVE(BK_BASIC, W5), TP(flt_type), FLT_LP24, TP(cut), 50, TP(res), 30, TP(fenv), 105,
         FLT(0, 50, 0, 50), AMP(0, 70, 0, 50), TRN(T_PLUCK, 80)),
    TONE("KOTO", PURE, WAVE(BK_PULSE, 30), TP(cut), 80, TP(fenv), 90, FLT(0, 50, 0, 50), AMP(0, 75, 0, 50),
         TRN(T_PICK, 110)),
    TONE("HARP", TO(ratio[1]), R2, TO(amt[0]), 30, TP(cut), 100, AMP(0, 85, 0, 80), TRN(T_PLUCK, 70)),
    TONE("NYLON", PURE, WAVE(BK_BASIC, W3), TP(cut), 75, TP(fenv), 85, FLT(0, 60, 0, 55), AMP(0, 80, 0, 55),
         TRN(T_PICK, 80)),
    TONE("SYNTH PLK", PURE, WAVE(BK_BASIC, W6), TP(cut), 70, TP(res), 60, TP(fenv), 100, FLT(0, 45, 0, 45),
         AMP(0, 60, 0, 45)),
    TONE("BANJO", PURE, WAVE(BK_PULSE, 90), TP(cut), 95, TP(fenv), 80, TP(hp_cut), 30, FLT(0, 45, 0, 40),
         AMP(0, 60, 0, 40), TRN(T_PICK, 110), TP(trn_tune[0]), 36),
    TONE("DIGIPLUCK", PURE, WAVE(BK_DIGI, 80), TP(cut), 105, TP(fenv), 80, FLT(0, 40, 30, 40), AMP(0, 60, 0, 50),
         TRN(T_BLIP, 70)),
    TONE("FM PLUCK", TO(ratio[1]), R1, TO(amt[0]), 90, TP(cut), 70, TP(fenv), 110, FLT(0, 45, 0, 45),
         AMP(0, 65, 0, 50)),
    /* brass, strings, winds */
    TONE("BRASS", WAVE(BK_BASIC, W5), DETUNED(71), TP(cut), 60, TP(fenv), 100, FLT(50, 70, 60, 45),
         AMP(30, 70, 120, 40), TRN(T_BRASS, 70)),
    TONE("SOFT HORN", PURE, WAVE(BK_BASIC, W4), TP(cut), 65, TP(fenv), 85, FLT(60, 80, 70, 50), AMP(45, 70, 120, 50)),
    TONE("CELLO", WAVE(BK_BASIC, W5), DETUNED(70), TP(cut), 70, TP(fenv), 70, FLT(50, 75, 60, 55),
         AMP(50, 70, 120, 60), VIB(80), TRN(T_BOW, 80)),
    TONE("PIZZ", WAVE(BK_BASIC, W5), DETUNED(72), TP(cut), 70, TP(fenv), 95, FLT(0, 50, 0, 45), AMP(0, 65, 0, 45),
         TRN(T_PLUCK, 50)),
    TONE("OBOE", PURE, WAVE(BK_PULSE, 85), TP(cut), 85, TP(res), 40, AMP(30, 70, 120, 35), TRN(T_BREATH, 50), VIB(70)),
    TONE("CLARINET", PURE, WAVE(BK_BASIC, W6), TP(cut), 80, TP(fenv), 70, AMP(30, 70, 120, 35), TRN(T_BREATH, 40)),
    TONE("PAN FLUTE", PURE, TP(noise_type), NOISE_PINK, TP(noise_lvl), 40, TP(noise_col), 100, TP(cut), 95,
         AMP(35, 70, 115, 45), TRN(T_CHIFF, 90), VIB(60)),
    TONE("SYNTHBRSS", WAVE(BK_BASIC, W5), DETUNED(72), TP(flt_type), FLT_LP24, TP(cut), 55, TP(res), 30, TP(fenv), 100,
         FLT(40, 75, 50, 45), AMP(15, 70, 120, 40)),
    /* effects, textures */
    TONE("NOISE SWP", TP(lvl1), 0, TP(noise_lvl), 110, TP(flt_type), FLT_LP24, TP(cut), 30, TP(res), 90, TP(fenv), 120,
         FLT(100, 110, 20, 90), AMP(40, 70, 120, 90)),
    TONE("LASER", PURE, TP(menv_dest), DEST_PITCH, TP(menv_amt), 127, TP(menv_a), 0, TP(menv_d), 55, TP(menv_s), 0,
         TP(cut), 110, AMP(0, 70, 0, 50), TRN(T_ZAP, 70)),
    TONE("WIND", TP(lvl1), 0, TP(noise_type), NOISE_PINK, TP(noise_lvl), 120, TP(cut), 55, TP(res), 80, TP(fenv), 64,
         TP(lfo_wave), LFO_TRI, TP(lfo_amt[1]), 100, TP(lfo_rate), 25, TP(lfo_retrig), 0, AMP(90, 80, 120, 95)),
    TONE("RADIO", PURE, WAVE(BK_DIGI, 100), TP(noise_type), NOISE_CRACKLE, TP(noise_lvl), 60, TP(hp_cut), 60,
         TP(cut), 80, TP(lfo_wave), LFO_SH, TP(lfo_dest[0]), DEST_PITCH, TP(lfo_amt[0]), 72, TP(lfo_rate), 100,
         TP(lfo_retrig), 0),
    TONE("METAL HIT", TO(ratio[1]), R7, TO(amt[0]), 100, TP(mfine[0][0]), 90, TO(ratio[2]), R11, TO(amt[1]), 80,
         TP(cut), 115, AMP(0, 90, 0, 85), TRN(T_METAL, 90)),
    TONE("S&H BLEEP", PURE, WAVE(BK_BASIC, W6), TP(cut), 60, TP(res), 90, TP(fenv), 64, TP(lfo_wave), LFO_SH,
         TP(lfo_rate), 95, TP(lfo_amt[1]), 110, TP(lfo_retrig), 0, AMP(0, 70, 120, 40)),
    TONE("DRONE", WAVE(BK_CMPLX, 100), TO(ratio[1]), R05, TO(amt[0]), 60, TP(flt_type), FLT_LP24, TP(cut), 70,
         TP(res), 40, TP(fenv), 64, TP(lfo_dest[1]), DEST_MOD1, TP(lfo_amt[1]), 90, TP(lfo_rate), 20,
         TP(lfo_retrig), 0, AMP(100, 80, 120, 110)),
    TONE("TALKBOX", PURE, WAVE(BK_VOCAL, W1), TP(menv_dest), DEST_WAVE1, TP(menv_amt), 110, TP(menv_a), 60,
         TP(menv_d), 60, TP(menv_s), 0, TP(menv_loop), 1, TP(cut), 100, AMP(5, 70, 120, 50)),
    /* synths */
    TONE("POLY SAW", WAVE(BK_BASIC, W5), DETUNED(70), TP(cut), 80, TP(res), 20, TP(fenv), 85, FLT(0, 70, 50, 50),
         AMP(5, 70, 110, 50)),
    TONE("STAB", WAVE(BK_BASIC, W5), DETUNED(73), TP(cut), 55, TP(res), 40, TP(fenv), 110, FLT(0, 45, 0, 35),
         AMP(0, 50, 0, 35)),
    TONE("HOOVER", WAVE(BK_PULSE, 30), DETUNED(80), TP(menv_dest), DEST_PITCH, TP(menv_amt), 50, TP(menv_a), 40,
         TP(menv_d), 60, TP(menv_s), 0, TP(cut), 90, AMP(0, 70, 120, 40), TP(glide), 35),
    TONE("PWM STRNG", PURE, WAVE(BK_PULSE, W4), TP(lfo_dest[1]), DEST_WAVE1, TP(lfo_amt[1]), 90, TP(lfo_rate), 45,
         TP(lfo_retrig), 0, TP(cut), 75, AMP(60, 80, 115, 75)),
    TONE("CHIPTUNE", PURE, WAVE(BK_PULSE, W3), TP(cut), 127, AMP(0, 70, 110, 10), TP(lfo_dest[0]), DEST_PITCH,
         TP(lfo_amt[0]), 70, TP(lfo_rate), 95, TP(lfo_delay), 60),
    TONE("SUPERSAW", WAVE(BK_BASIC, W5), DETUNED(82), TO(mode[1]), MODE_MIX, TO(bank[2]), BK_BASIC, TO(pos[2]), W5,
         TP(mpitch[0][1]), 24, TP(mfine[0][1]), 50, TO(amt[1]), 100, TP(cut), 95, AMP(5, 70, 115, 55)),
    TONE("RAVE ORG", PURE, WAVE(BK_HARM, W5), TO(ratio[1]), R2, TO(amt[0]), 40, TP(cut), 85, TP(res), 30, TP(fenv), 90,
         FLT(0, 40, 30, 30), AMP(0, 60, 80, 30)),
    TONE("BRIGHT KY", TO(ratio[1]), R2, TO(amt[0]), 70, TO(ratio[2]), R4, TO(amt[1]), 30, TP(cut), 110,
         AMP(0, 80, 30, 55), TRN(T_TICK, 60)),
    /* percussion */
    TONE("TOM", PURE, TP(menv_dest), DEST_PITCH, TP(menv_amt), 95, TP(menv_a), 0, TP(menv_d), 45, TP(menv_s), 0,
         TP(cut), 90, AMP(0, 60, 0, 55), TRN(T_THUMP, 80)),
    TONE("COWBELL", PURE, WAVE(BK_PULSE, W1), TO(mode[0]), MODE_MIX, TO(bank[1]), BK_PULSE, TO(pos[1]), W1,
         TP(mpitch[0][0]), 32, TO(amt[0]), 120, TP(cut), 95, TP(res), 40, TP(hp_cut), 50, AMP(0, 45, 0, 40),
         TRN(T_CLICK, 60)),
    TONE("CONGA", PURE, TP(menv_dest), DEST_PITCH, TP(menv_amt), 75, TP(menv_a), 0, TP(menv_d), 35, TP(menv_s), 0,
         AMP(0, 50, 0, 45), TRN(T_SLAP, 90)),
    TONE("WOODBLOCK", PURE, TO(ratio[1]), R3, TO(amt[0]), 40, TP(hp_cut), 40, AMP(0, 35, 0, 30), TRN(T_KNOCK, 110)),
    TONE("TIMPANI", TO(ratio[1]), R1H, TO(amt[0]), 30, TP(cut), 70, TP(menv_dest), DEST_PITCH, TP(menv_amt), 70,
         TP(menv_d), 50, TP(menv_s), 0, AMP(0, 90, 0, 85), TRN(T_THUMP, 100)),
    TONE("SNAP", TP(lvl1), 0, TP(noise_lvl), 120, TP(hp_cut), 60, TP(cut), 110, AMP(0, 35, 0, 30), TRN(T_SLAP, 100)),
    TONE("CLAVES", PURE, TO(ratio[1]), R5, TO(amt[0]), 25, TP(cut), 120, AMP(0, 30, 0, 25), TRN(T_TICK, 100),
         TP(trn_tune[0]), 36),
    TONE("TABLA", PURE, TP(menv_dest), DEST_PITCH, TP(menv_amt), 58, TP(menv_a), 0, TP(menv_d), 50, TP(menv_s), 0,
         TO(ratio[1]), R2, TO(amt[0]), 20, AMP(0, 60, 0, 50), TRN(T_KNOCK, 80)),
    /* voices */
    TONE("AAH CHOIR", WAVE(BK_VOCAL, W3), TO(mode[0]), MODE_MIX, TO(bank[1]), BK_VOCAL, TO(pos[1]), W3,
         TP(mpitch[0][0]), 24, TP(mfine[0][0]), 74, TO(amt[0]), 100, TP(cut), 90, AMP(85, 80, 115, 90), VIB(50)),
    TONE("OOH PAD", WAVE(BK_VOCAL, W1), TO(mode[0]), MODE_MIX, TO(bank[1]), BK_VOCAL, TO(pos[1]), W2,
         TP(mpitch[0][0]), 24, TP(mfine[0][0]), 70, TO(amt[0]), 90, TP(cut), 70, AMP(90, 80, 110, 90)),
    TONE("FORMANT", PURE, WAVE(BK_VOCAL, W1), TP(lfo_wave), LFO_TRI, TP(lfo_dest[1]), DEST_WAVE1,
         TP(lfo_amt[1]), 110, TP(lfo_rate), 50, AMP(5, 70, 120, 40)),
    TONE("ROBOT", PURE, WAVE(BK_VOCAL, W5), TO(mode[0]), MODE_RING, TP(mpitch[0][0]), 24, TO(amt[0]), 60,
         TP(lfo_wave), LFO_SH, TP(lfo_dest[1]), DEST_WAVE1, TP(lfo_amt[1]), 120, TP(lfo_rate), 90, TP(lfo_retrig), 0,
         AMP(0, 70, 120, 20)),
    TONE("WHISPER", TP(lvl1), 0, TP(noise_type), NOISE_PINK, TP(noise_lvl), 120, TP(flt_type), FLT_COMBP, TP(cut), 80,
         TP(res), 70, TP(comb_damp), 60, AMP(40, 70, 120, 60)),
    TONE("MONK", PURE, WAVE(BK_VOCAL, W2), TO(ratio[0]), R05, TP(cut), 60, AMP(70, 80, 120, 80), VIB(60)),
    TONE("VOX PLUCK", PURE, WAVE(BK_VOCAL, W5), TP(menv_dest), DEST_WAVE1, TP(menv_amt), 30, TP(menv_a), 0,
         TP(menv_d), 50, TP(menv_s), 0, AMP(0, 60, 0, 45), TRN(T_VOX, 70)),
    TONE("VOCODER", WAVE(BK_VOCAL, W4), TO(mode[0]), MODE_RING, TO(bank[1]), BK_BASIC, TO(pos[1]), W5,
         TP(mpitch[0][0]), 24, TO(amt[0]), 90, TP(lfo_dest[1]), DEST_WAVE1, TP(lfo_amt[1]), 100, TP(lfo_rate), 40,
         TP(lfo_retrig), 0, AMP(5, 70, 120, 40)),
    /* atmospheres */
    TONE("RAIN", TP(lvl1), 0, TP(noise_type), NOISE_CRACKLE, TP(noise_lvl), 120, TP(hp_cut), 40, TP(cut), 90,
         AMP(60, 80, 120, 90)),
    TONE("SPACE", TO(ratio[1]), R1H, TO(amt[0]), 40, TP(mfine[0][0]), 76, TO(att[0]), 100, TP(cut), 90,
         AMP(100, 90, 120, 110), TP(lfo_dest[1]), DEST_MOD1, TP(lfo_amt[1]), 90, TP(lfo_rate), 20, TP(lfo_retrig), 0),
    TONE("BOWL", TO(ratio[1]), R3, TO(amt[0]), 30, TP(mfine[0][0]), 70, TP(cut), 120, AMP(0, 110, 0, 110),
         TRN(T_BELL, 40), TP(lfo_dest[0]), DEST_AMP, TP(lfo_amt[0]), 72, TP(lfo_rate), 60),
    TONE("GHOST", PURE, WAVE(BK_VOCAL, W8), TP(noise_type), NOISE_PINK, TP(noise_lvl), 50, TP(glide), 70,
         AMP(90, 80, 120, 100), TP(lfo_wave), LFO_TRI, TP(lfo_dest[0]), DEST_PITCH, TP(lfo_amt[0]), 76,
         TP(lfo_rate), 50),
    TONE("MACHINE", WAVE(BK_DIGI, W6), TO(ratio[1]), R7, TO(amt[0]), 70, TP(lfo_wave), LFO_SQR, TP(lfo_dest[1]),
         DEST_MOD1, TP(lfo_amt[1]), 110, TP(lfo_rate), 85, TP(lfo_retrig), 0, AMP(0, 70, 120, 40)),
    TONE("STATIC", TP(lvl1), 0, TP(noise_type), NOISE_DIGI, TP(noise_lvl), 110, TP(noise_col), 90, TP(flt_type), FLT_LP24,
         TP(cut), 70, TP(res), 80, TP(lfo_wave), LFO_SH, TP(lfo_amt[1]), 110, TP(lfo_rate), 100, TP(lfo_retrig), 0,
         AMP(0, 70, 120, 50)),
    TONE("DEEP SEA", WAVE(BK_BASIC, W1), TO(ratio[0]), R05, TP(flt_type), FLT_LP24, TP(cut), 35, TP(res), 90,
         TP(fenv), 64, TP(noise_type), NOISE_BROWN, TP(noise_lvl), 80, TP(lfo_wave), LFO_TRI, TP(lfo_amt[1]), 100,
         TP(lfo_rate), 15, TP(lfo_retrig), 0, AMP(100, 80, 120, 110)),
    TONE("CHIMES", TO(ratio[1]), R7, TO(amt[0]), 45, TP(mfine[0][0]), 85, TP(cut), 125, AMP(0, 100, 0, 100),
         TRN(T_GLASS, 60), TP(trn_tune[0]), 36),
};
#undef TONE

static void tone_rom(uint32_t i, tone_t *t)     /* factory tone i (0 .. TONE_ROM_N - 1) */
{
    static patch_t p;                           /* (static: off the main loop's stack) */
    uint32_t k;
    const uint8_t *kv = TONE_ROM[i % TONE_ROM_N].kv;
    p = PATCH_INIT;
    for (; kv[0] != TE; kv += 2)
        ((uint8_t *)&p)[kv[0]] = kv[1];
    tone_get(&p, 0, t);
    memset(t->name, 0, TONE_NAME);
    for (k = 0; k < TONE_NAME - 1u && TONE_ROM[i % TONE_ROM_N].name[k]; k++) t->name[k] = TONE_ROM[i % TONE_ROM_N].name[k];
}

/* ---- the user tones */
static void tones_half(uint32_t h)              /* half h in RAM: as stored, or empty */
{
    if (tone_user_h == h) return;
    if (seqstore_load(TONE_AREA + h, &tone_user, sizeof tone_user) != 0 || tone_user.version != TONE_VERSION) {
        memset(&tone_user, 0, sizeof tone_user);
        tone_user.version = TONE_VERSION;
    }
    tone_user_h = (uint8_t)h;
}
static int tone_user_get(uint32_t i, tone_t *t)   /* user tone i (0 .. 63); 0 = there is one */
{
    const tone_t *u;
    tones_half((i / TONE_HALF) & 1u);
    u = &tone_user.t[i % TONE_HALF];
    if (!u->name[0]) return -1;
    *t = *u;
    return 0;
}
static int tone_user_put(uint32_t i, const tone_t *t)   /* into user tone i, saved (its half); 0 = done */
{
    tones_half((i / TONE_HALF) & 1u);
    tone_user.t[i % TONE_HALF] = *t;
    return seqstore_save(TONE_AREA + tone_user_h, &tone_user, sizeof tone_user);
}
static int tone_at(uint32_t i, tone_t *t)       /* the browser's tone i: 0 .. 63 the factory's, 64 .. 127 the user's */
{
    if (i < TONE_ROM_N) { tone_rom(i, t); return 0; }
    return tone_user_get(i - TONE_ROM_N, t);
}
_Static_assert(sizeof(tone_half_t) <= 4080u, "a half of the user tones fits a store area");
