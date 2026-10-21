/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's pages (docs/PRYSM-DESIGN.md): OSC 1, OSC 2, FILTER, AMP, LFO, LFO 2, MIX, ARP, SEQ, CHORUS, DELAY,
 * REVERB, SETUP.
 *
 *   ALGORITHM        the page (or EDIT = OSC 1 / OSC 2, ENV = AMP / FILTER, LFO = LFO / LFO 2, GLO = MIX / SETUP, ARP = ARP
 *                    (on the ARP page: the arp on / off), SEQ = SEQ, FX = CHORUS / DELAY / REVERB,
 *                    the SELECT button = FILTER)
 *   PLAY             the sequencer: run / stop (anywhere; its LED shows it)
 *   REC              step entry on the SEQ page: a key writes its note at the cursor, which moves on
 *   PRESETS          the filter's cutoff, on every page (the header shows it a moment)
 *   SELECT           the row
 *   KNOB1 .. KNOB4   the row's parameters, left to right
 *   HOME             the oscilloscope (KNOB1: how many cycles); HOME again, ALGORITHM or a page button: back
 *   SAVE             the PATCH screen: 8 slots; SELECT = SAVE / LOAD / INIT, KNOB1 = the slot, SAVE again = do it
 *                    (twice over a used slot or unsaved changes); HOME or a page = back
 *
 * The screen, top to bottom: the header (the page and the row; the page number and the CPU), the page's graphic
 * (what the row being edited touches, lit), the legend (the row's four labels, over the knobs' cells), the rows (a
 * marker each, as in the sketch, then the four cells). Only what changed is redrawn, at most every UI_FRAME_MS.
 * Pages are tables (page_t -> row_t -> prm_t): a parameter is a field of patch_t, its range and how it shows. */

/* the colours: white and greys on black */
#define C_ACC RGB(255, 255, 255)                /* what is being edited, where we are */
#define C_BRIGHT RGB(255, 255, 255)             /* what is being touched */
#define C_MID RGB(150, 150, 150)                /* values, curves not being edited */
#define C_FAINT RGB(72, 72, 72)                 /* guides, dotted lines, what is off */
#define C_BOX RGB(56, 56, 56)                   /* frames */
#define C_SEL RGB(40, 40, 40)                   /* the selected row's band */
#define C_FILL RGB(34, 34, 34)                  /* under a curve */

#include "prysm_font_m.h"                       /* FONT_M: the focus row's values (Terminus 22 bold, 11 x 22) */
#include "prysm_font_p.h"                       /* FONT_P: the pop-up's value (Terminus 32 bold, 16 x 32) */

/* the screen, top to bottom: the header (the page > the row; the slot, unsaved, the CPU), the picture, the focus
 * row (the selected row's four labels and values, large, a bar under each), the tabs (every row of the page) */
#define HDR_Y 0u
#define G_Y 21u                                 /* the picture: 240 x G_H */
#define G_H 124u
#define FOC_Y 151u                              /* the focus row: labels, values, bars */
#define FOC_H 52u
#define FOC_W 60                                /* a cell each knob */
#define TAB_Y 216u                              /* the tabs */
#define TAB_H 22u
#ifndef POP_MS
#define POP_MS 1200u                            /* a knob turned: its name and value, large, in the picture's corner */
#endif
#define UI_FRAME_MS 30u

/* ---- the parameters */
enum { K_NONE, K_NUM, K_BIP, K_CENT, K_BANK, K_POS, K_RATIO, K_MODE, K_SEMI, K_LFOW, K_RATE, K_ONOFF, K_DEST,
       K_DMS, K_PMS, K_SHIFT, K_ARPM, K_DIV, K_OCT, K_BPM, K_GATE, K_SWING, K_GLIDEM, K_MCHI, K_MCHO, K_CLK,
       K_CURSOR, K_SNOTE, K_STRIG, K_SSLIDE, K_SLEN, K_HPF, K_FTYPE, K_LDEST, K_L2DEST, K_L2SYNC, K_BEND };   /* (the step kinds: the step under the cursor) */
typedef struct { const char *label; uint16_t off; uint8_t max, kind; } prm_t;   /* off: in patch_t (osc pages: in osc[0]) */
typedef struct { const char *name; prm_t p[4]; uint16_t lit; } row_t;  /* lit: what the graphic lights (LIT_...) */
typedef struct { const char *name; const row_t *rows; uint8_t n, osc; } page_t;   /* osc: 1, 2, 0 = none; G: gset */

/* the global settings (not the sound: the SETUP page, the octave, the slot), kept in the settings sector by the app.
 * Fields go at the end: an older, shorter record still loads (prysm.c) */
typedef struct {
    int8_t oct, slot;                           /* the octave (-2 .. 2), the last slot saved or loaded (-1: none) */
    uint8_t midi_in;                            /* 0 = OMNI, 1 .. 16 */
    uint8_t midi_out;                           /* 0 .. 15 = channel 1 .. 16 */
    uint8_t clock;                              /* the arp: 0 its own, 1 the MIDI clock */
    uint8_t bend;                               /* MIDI pitch bend's range: 0 .. 24 semitones */
} gset_t;
#define GSET_V1_SIZE 5u                         /* (before bend) */
static gset_t gset = {0, -1, 0, 0, 0, 2};
static uint8_t gset_dirty;                      /* changed: the app saves it (when quiet) */
#define G 3u                                    /* (page_t.osc: the page edits gset) */
#define GO(f) ((uint16_t)__builtin_offsetof(gset_t, f))

enum { LIT_C = 1, LIT_M1 = 2, LIT_M2 = 4, LIT_A1 = 8, LIT_A2 = 16, LIT_CURVE = 32, LIT_ENV = 64, LIT_ALL = 127,
       LIT_AR = 128,                            /* (OSC pages: the mods' AR shapes under their boxes, not the names) */
       LIT_MENV = 256,                          /* (the LFO page: the mod envelope's picture, not the LFO's) */
       LIT_L2 = 512 };                          /* (the LFO page: LFO 2's picture) */
#define OO(f) ((uint16_t)__builtin_offsetof(patch_t, osc[0].f))
#define PO(f) ((uint16_t)__builtin_offsetof(patch_t, f))
#define NOPRM {0, 0, 0, K_NONE}

static const row_t OSC_ROWS[] = {
    {"WAVE", {{"BANK", OO(bank[0]), PW_BANKS - 1u, K_BANK}, {"WAVE", OO(pos[0]), 127, K_POS},
              {"BANK", OO(bank[1]), PW_BANKS - 1u, K_BANK}, {"WAVE", OO(pos[1]), 127, K_POS}}, LIT_C | LIT_M1},
    {"WAVE2", {{"BANK", OO(bank[2]), PW_BANKS - 1u, K_BANK}, {"WAVE", OO(pos[2]), 127, K_POS}, NOPRM, NOPRM},
     LIT_M2},
    {"RATIO", {{"CAR", OO(ratio[0]), PR_RATIOS - 1u, K_RATIO}, {"MOD1", OO(ratio[1]), PR_RATIOS - 1u, K_RATIO},
               {"MOD2", OO(ratio[2]), PR_RATIOS - 1u, K_RATIO}, {"FINE", OO(fine), 127, K_CENT}},
     LIT_C | LIT_M1 | LIT_M2},
    {"MODE", {{"MOD1", OO(mode[0]), MODE_N - 1u, K_MODE}, {"MOD2", OO(mode[1]), MODE_N - 1u, K_MODE}, NOPRM, NOPRM},
     LIT_A1 | LIT_A2},
    {"ENV", {{"A1", OO(att[0]), 127, K_NUM}, {"R1", OO(rel[0]), 127, K_NUM},
             {"A2", OO(att[1]), 127, K_NUM}, {"R2", OO(rel[1]), 127, K_NUM}}, LIT_M1 | LIT_M2 | LIT_AR},
    {"MIX", {{"AMT1", OO(amt[0]), 127, K_NUM}, {"AMT2", OO(amt[1]), 127, K_NUM},
             {"LEVEL", OO(level), 127, K_NUM}, NOPRM}, LIT_A1 | LIT_A2 | LIT_C},
};
static const row_t FILTER_ROWS[] = {
    {"FILTER", {{"CUT", PO(cut), 127, K_NUM}, {"RES", PO(res), 127, K_NUM},
                {"ENV", PO(fenv), 127, K_BIP}, {"KEY", PO(key), 127, K_NUM}}, LIT_CURVE},
    {"ENV", {{"A", PO(flt_a), 127, K_NUM}, {"D", PO(flt_d), 127, K_NUM},
             {"S", PO(flt_s), 127, K_NUM}, {"R", PO(flt_r), 127, K_NUM}}, LIT_ENV},
    {"TYPE", {{"TYPE", PO(flt_type), FLT_N - 1u, K_FTYPE}, {"DRIVE", PO(drive), 127, K_NUM},
              {"HPF", PO(hp_cut), 127, K_HPF}, NOPRM}, LIT_CURVE},
    {"COMB", {{"DAMP", PO(comb_damp), 127, K_NUM}, {"SPREAD", PO(comb_spread), 127, K_NUM}, NOPRM, NOPRM}, LIT_CURVE},
};
static const row_t AMP_ROWS[] = {
    {"ENV", {{"A", PO(amp_a), 127, K_NUM}, {"D", PO(amp_d), 127, K_NUM},
             {"S", PO(amp_s), 127, K_NUM}, {"R", PO(amp_r), 127, K_NUM}}, LIT_ENV},
    {"VEL", {{"AMP", PO(vel_amp), 127, K_NUM}, {"CUTOFF", PO(vel_cut), 127, K_NUM},     /* MIDI velocity */
             {"MOD", PO(vel_mod), 127, K_NUM}, NOPRM}, LIT_ENV},
};
static const row_t LFO_ROWS[] = {
    {"LFO", {{"WAVE", PO(lfo_wave), LFO_N - 1u, K_LFOW}, {"RATE", PO(lfo_rate), 127, K_RATE},
             {"DELAY", PO(lfo_delay), 127, K_NUM}, {"RETRIG", PO(lfo_retrig), 1, K_ONOFF}}, LIT_CURVE},
    {"DEST", {{"DEST1", PO(lfo_dest[0]), DEST_N - 1u, K_LDEST}, {"AMT1", PO(lfo_amt[0]), 127, K_BIP},
              {"DEST2", PO(lfo_dest[1]), DEST_N - 1u, K_LDEST}, {"AMT2", PO(lfo_amt[1]), 127, K_BIP}}, LIT_ENV},
    {"M.ENV", {{"A", PO(menv_a), 127, K_NUM}, {"D", PO(menv_d), 127, K_NUM},
               {"S", PO(menv_s), 127, K_NUM}, {"R", PO(menv_r), 127, K_NUM}}, LIT_MENV | LIT_ENV},
    {"M.DST", {{"LOOP", PO(menv_loop), 1, K_ONOFF}, {"DEST", PO(menv_dest), DEST_N - 1u, K_DEST},
               {"AMT", PO(menv_amt), 127, K_BIP}, NOPRM}, LIT_MENV | LIT_CURVE},
};
static const row_t LFO2_ROWS[] = {               /* (one for all the voices; SYNC: the arp's tempo) */
    {"LFO", {{"WAVE", PO(lfo2_wave), LFO_N - 1u, K_LFOW}, {"RATE", PO(lfo2_rate), 127, K_RATE},
              {"SYNC", PO(lfo2_sync), L2SYNC_N - 1u, K_L2SYNC}, NOPRM}, LIT_L2 | LIT_CURVE},
    {"DEST", {{"DEST", PO(lfo2_dest), DEST2_N - 1u, K_L2DEST}, {"AMT", PO(lfo2_amt), 127, K_BIP}, NOPRM, NOPRM},
     LIT_L2 | LIT_ENV},
};
static const row_t SEQ_ROWS[] = {                /* (STEP: the step under the cursor) */
    {"STEP", {{"STEP", 0, 63, K_CURSOR}, {"NOTE", PO(seq_note), 127, K_SNOTE},
              {"TRIG", PO(seq_trig), SEQ_TRIGS - 1u, K_STRIG}, {"SLIDE", PO(seq_slide), 1, K_SSLIDE}}, LIT_CURVE},
    {"PATTERN", {{"LENGTH", PO(seq_len), 64, K_SLEN}, {"DIV", PO(seq_div), DIV_N - 1u, K_DIV},
                 {"GATE", PO(seq_gate), 127, K_GATE}, {"BPM", PO(arp_bpm), 200, K_BPM}}, LIT_ENV},
};
static const row_t SETUP_ROWS[] = {
    {"MIDI", {{"IN", GO(midi_in), 16, K_MCHI}, {"OUT", GO(midi_out), 15, K_MCHO},
              {"CLOCK", GO(clock), 1, K_CLK}, {"BEND", GO(bend), 24, K_BEND}}, LIT_ALL},
};
/* the effects, a page each (the FX button steps through them); lit: LIT_C the chorus, LIT_M1 the delay, LIT_M2 the
 * reverb */
static const row_t CHO_ROWS[] = {
    {"CHORUS", {{"RATE", PO(cho_rate), 127, K_RATE}, {"DEPTH", PO(cho_depth), 127, K_NUM},
                {"MIX", PO(cho_mix), 127, K_NUM}, NOPRM}, LIT_C},
};
static const row_t DLY_ROWS[] = {
    {"DELAY", {{"TIME", PO(dly_time), 127, K_DMS}, {"FDBK", PO(dly_fb), 127, K_NUM},
               {"MIX", PO(dly_mix), 127, K_NUM}, {"PING", PO(dly_ping), 1, K_ONOFF}}, LIT_M1},
    {"D.MOD", {{"WOW", PO(dly_wrate), 127, K_RATE}, {"DEPTH", PO(dly_wdepth), 127, K_NUM},
               {"SHIFT", PO(dly_shift), 24, K_SHIFT}, {"TONE", PO(dly_tone), 127, K_NUM}}, LIT_M1},
};
static const row_t REV_ROWS[] = {
    {"REVERB", {{"SIZE", PO(rev_size), 127, K_NUM}, {"DAMP", PO(rev_damp), 127, K_NUM},
                {"MIX", PO(rev_mix), 127, K_NUM}, {"PRE", PO(rev_pre), 127, K_PMS}}, LIT_M2},
    {"R.MOD", {{"RATE", PO(rev_mrate), 127, K_RATE}, {"DEPTH", PO(rev_mdepth), 127, K_NUM},
               {"SHIMMR", PO(rev_shimmer), 127, K_NUM}, NOPRM}, LIT_M2},
};
static const row_t MIX_ROWS[] = {
    {"OSC", {{"OSC1", PO(lvl1), 127, K_NUM}, {"OSC2", PO(lvl2), 127, K_NUM},
             {"SEMI", PO(semi2), 48, K_SEMI}, {"FINE", PO(fine2), 127, K_CENT}}, LIT_ALL},
    {"GLIDE", {{"TIME", PO(glide), 127, K_NUM}, {"MODE", PO(glide_mode), GLIDE_N - 1u, K_GLIDEM}, NOPRM, NOPRM}, LIT_ALL},
};
static const row_t ARP_ROWS[] = {
    {"ARP", {{"ARP", PO(arp_on), 1, K_ONOFF}, {"MODE", PO(arp_mode), ARP_N - 1u, K_ARPM},
             {"DIV", PO(arp_div), DIV_N - 1u, K_DIV}, {"OCT", PO(arp_oct), 3, K_OCT}}, LIT_CURVE},
    {"TIME", {{"BPM", PO(arp_bpm), 200, K_BPM}, {"GATE", PO(arp_gate), 127, K_GATE},
              {"SWING", PO(arp_swing), 127, K_SWING}, {"LATCH", PO(arp_latch), 1, K_ONOFF}}, LIT_CURVE},
};
#define NROWS(a) ((uint8_t)(sizeof a / sizeof a[0]))
enum { PG_OSC1, PG_OSC2, PG_FILTER, PG_AMP, PG_LFO, PG_LFO2, PG_MIX, PG_ARP, PG_SEQ, PG_CHO, PG_DLY, PG_REV, PG_SETUP, PG_N };
static const page_t PAGES[PG_N] = {
    {"OSC 1", OSC_ROWS, NROWS(OSC_ROWS), 1}, {"OSC 2", OSC_ROWS, NROWS(OSC_ROWS), 2},
    {"FILTER", FILTER_ROWS, NROWS(FILTER_ROWS), 0}, {"AMP", AMP_ROWS, NROWS(AMP_ROWS), 0},
    {"LFO", LFO_ROWS, NROWS(LFO_ROWS), 0}, {"LFO 2", LFO2_ROWS, NROWS(LFO2_ROWS), 0},
    {"MIX", MIX_ROWS, NROWS(MIX_ROWS), 0},
    {"ARP", ARP_ROWS, NROWS(ARP_ROWS), 0}, {"SEQ", SEQ_ROWS, NROWS(SEQ_ROWS), 0}, 
    {"CHORUS", CHO_ROWS, NROWS(CHO_ROWS), 0}, {"DELAY", DLY_ROWS, NROWS(DLY_ROWS), 0},
    {"REVERB", REV_ROWS, NROWS(REV_ROWS), 0},
    {"SETUP", SETUP_ROWS, NROWS(SETUP_ROWS), G},
};

static patch_t edit;                            /* the sound being edited (engine_set() after each change) */
static patch_t clean;                           /* the sound as last loaded or saved: edit differs = unsaved changes */
static int32_t cur_slot = -1;                   /* the slot it came from or went to (-1: none) */
static uint8_t slot_moved;                      /* cur_slot changed: the app keeps it for the next power-on */
static int modified(void) { return memcmp(&edit, &clean, sizeof edit) != 0; }
static uint32_t page, row[PG_N];
static uint32_t dirty;                          /* what to redraw: D_... */
enum { D_HDR = 1, D_GFX = 2, D_LEG = 4, D_ROWS = 8, D_ALL = 15 };

static uint8_t seq_cursor;                      /* the SEQ page's step (0..63) */
static uint8_t seq_rec;                         /* REC: keys write notes at the cursor */

static uint8_t *pval(uint32_t pg, const prm_t *p)
{
    uint32_t base;
    if (p->kind == K_CURSOR)
        return &seq_cursor;
    if (p->kind == K_SNOTE || p->kind == K_STRIG || p->kind == K_SSLIDE)   /* the step under the cursor */
        return (uint8_t *)&edit + p->off + seq_cursor;
    if (PAGES[pg].osc == G)
        return (uint8_t *)&gset + p->off;
    base = PAGES[pg].osc ? (PAGES[pg].osc - 1u) * (uint32_t)sizeof edit.osc[0] : 0u;
    return (uint8_t *)&edit + p->off + base;
}

/* ---- text */
static char *put(char *s, const char *a) { while (*a) *s++ = *a++; *s = 0; return s; }
static char *num(char *s, uint32_t v)
{
    char b[10];
    uint32_t i = 0;
    do b[i++] = (char)('0' + v % 10u); while (v /= 10u);
    while (i) *s++ = b[--i];
    *s = 0;
    return s;
}
static char *snum(char *s, int32_t v)           /* signed, with its sign (0 without) */
{
    if (v > 0) *s++ = '+';
    if (v < 0) { *s++ = '-'; v = -v; }
    return num(s, (uint32_t)v);
}

static uint32_t wave_index(uint32_t pos)        /* the nearest of the bank's 8 waves */
{
    return ((pos & 127u) * (PW_WAVES - 1u) * 256u / 127u + 128u) >> 8;
}

static void hz(char *s, int32_t q)              /* quarter semitones from MIDI 0 -> Hz (to the half semitone) */
{
    uint32_t h = ((8176u * PR_EXP2_24_Q12[(q % 48) >> 1]) >> 12) << (q / 48);   /* mHz: 8.176 Hz x 2^(q / 48) */
    if (h < 1000000u) put(num(s, (h + 500u) / 1000u), "Hz");
    else { char *e = num(s, h / 1000000u); *e++ = '.'; put(num(e, h / 100000u % 10u), "k"); }
}

static void fmt(char *s, const prm_t *p, const uint8_t *v)
{
    switch (p->kind) {
    case K_BIP: snum(s, (int32_t)*v - 64); break;
    case K_CENT: put(snum(s, ((int32_t)*v - 64) * 50 / 63), "c"); break;
    case K_BANK: put(s, PW_BANK_NAME[*v < PW_BANKS ? *v : 0u]); break;
    case K_POS: put(s, PW_WAVE_NAME[v[-(int)NOP] < PW_BANKS ? v[-(int)NOP] : 0u][wave_index(*v)]); break;
    case K_RATIO: put(put(s, "x"), PR_RATIO_NAME[*v < PR_RATIOS ? *v : 1u]); break;
    case K_MODE: put(s, MODE_NAME[*v < MODE_N ? *v : 0u]); break;
    case K_SEMI: put(snum(s, (int32_t)*v - 24), "st"); break;
    case K_LFOW: put(s, LFO_NAME[*v < LFO_N ? *v : 0u]); break;
    case K_DEST: case K_LDEST: put(s, DEST_NAME[*v < DEST_N ? *v : 0u]); break;
    case K_L2DEST: put(s, DEST_NAME[*v < DEST2_N ? *v : 0u]); break;
    case K_L2SYNC: put(s, L2SYNC_NAME[*v < L2SYNC_N ? *v : 0u]); break;
    case K_ONOFF: put(s, *v ? "ON" : "OFF"); break;
    case K_DMS: put(num(s, (441u + (uint32_t)*v * (DLY_N - 1100u) / 127u) * 1000u / 44100u), "ms"); break;
    case K_PMS: put(num(s, (1u + (uint32_t)*v * (PRE_N - 2u) / 127u) * 1000u / 44100u), "ms"); break;
    case K_SHIFT: put(snum(s, (int32_t)*v - 12), "st"); break;
    case K_ARPM: put(s, ARP_NAME[*v < ARP_N ? *v : 0u]); break;
    case K_DIV: put(s, DIV_NAME[*v < DIV_N ? *v : DIV_16]); break;
    case K_OCT: num(s, *v + 1u); break;
    case K_BPM:
        if (gset.clock) {                       /* on the MIDI clock: its tempo, or EXT while none comes */
            uint32_t b = mc_bpm10;
            if (b) { char *e = num(s, b / 10u); *e++ = '.'; num(e, b % 10u); }
            else put(s, "EXT");
        } else
            num(s, *v + 40u);
        break;
    case K_GATE: if (*v >= 127u) put(s, "TIE"); else put(num(s, 5u + *v * 95u / 127u), "%"); break;
    case K_SWING: put(num(s, 50u + *v * 25u / 127u), "%"); break;   /* the even steps' share of a pair */
    case K_GLIDEM: put(s, GLIDE_NAME[*v < GLIDE_N ? *v : 0u]); break;
    case K_MCHI: if (*v) num(s, *v); else put(s, "OMNI"); break;
    case K_MCHO: num(s, *v + 1u); break;
    case K_CLK: put(s, *v ? "MIDI" : "INT"); break;
    case K_BEND: if (*v) put(num(put(s, "+-"), *v), "st"); else put(s, "OFF"); break;
    case K_CURSOR: num(s, *v + 1u); break;
    case K_SNOTE: {
        static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        num(put(s, NM[*v % 12u]), *v / 12u ? *v / 12u - 1u : 0u);
        break;
    }
    case K_STRIG: put(s, SEQ_TRIG_NAME[*v < SEQ_TRIGS ? *v : 0u]); break;
    case K_SSLIDE: put(s, *v ? "ON" : "OFF"); break;
    case K_SLEN: num(s, *v); break;
    case K_HPF: if (*v) hz(s, hp_q(*v)); else put(s, "OFF"); break;
    case K_FTYPE: put(s, FLT_NAME[*v < FLT_N ? *v : 0u]); break;
    case K_RATE: {                              /* Hz: 0.05 .. 40 */
        uint32_t h = PR_LFO_HZ100[*v & 127u];
        char *e = num(s, h / 100u);
        if (h < 1000u) {
            *e++ = '.';
            if (h < 100u) *e++ = (char)('0' + h / 10u % 10u), e = num(e, h % 10u);
            else e = num(e, h / 10u % 10u);
        }
        put(e, "Hz");
        break;
    }
    default: num(s, *v);
    }
}

/* ---- canvas helpers */
static void cv_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c, int fill)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y;
            if (fill ? d <= r * r : (d <= r * r && d > (r - 1) * (r - 1)))
                cv_pset(cx + x, cy + y, c);
        }
}

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}

/* the battery (fm1_batt: 0 .. 3 bars, 4 on USB power: a bolt), 17 x 9 px times s; empty, it blinks */
static void cv_batt(int32_t x, int32_t y, uint32_t lvl, int32_t s)
{
    static const char BOLT[6][14] = {".......##....", "......##.....", ".....######..", "........##...",
                                     ".......##....", "......#......"};
    uint16_t c = lvl == 0u ? ((fm1_ms / 500u) & 1u ? C_BRIGHT : C_FAINT) : C_MID;
    int32_t i, j;
    cv_rect(x, y, 15 * s, 9 * s, c);            /* the case, and its tip */
    cv_rect(x + s, y + s, 13 * s, 7 * s, C_BLACK);
    cv_rect(x + 15 * s, y + 3 * s, 2 * s, 3 * s, c);
    if (lvl == 4u) {
        for (j = 0; j < 6; j++)
            for (i = 0; i < 13; i++)
                if (BOLT[j][i] == '#') cv_rect(x + (1 + i) * s, y + (2 + j) * s, s, s, C_BRIGHT);
        return;
    }
    for (i = 0; i < (int32_t)lvl && i < 3; i++)
        cv_rect(x + (2 + 4 * i) * s, y + 2 * s, 3 * s, 5 * s, C_BRIGHT);
}

static void cv_hdots(int32_t x0, int32_t x1, int32_t y, int32_t step)   /* a guide: dotted, faint */
{
    for (; x0 < x1; x0 += step) cv_pset(x0, y, C_FAINT);
}

static void cv_vdots(int32_t x, int32_t y0, int32_t y1, int32_t step)
{
    for (; y0 < y1; y0 += step) cv_pset(x, y0, C_FAINT);
}

static void cv_under(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t base)   /* the fill under a segment */
{
    int32_t x;
    for (x = x0; x <= x1; x++) {
        int32_t y = x1 > x0 ? y0 + (y1 - y0) * (x - x0) / (x1 - x0) : y1;
        if (base > y) cv_rect(x, y, 1, base - y, C_FILL);
        else if (y > base) cv_rect(x, base, 1, y - base, C_FILL);
    }
}

static void cv_adsr(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t a, uint32_t d, uint32_t s, uint32_t r,
                    uint16_t c)                 /* an envelope's shape: times as widths, the sustain as a height */
{
    int32_t seg = w / 4, aw = 1 + (int32_t)a * (seg - 1) / 127, dw = 1 + (int32_t)d * (seg - 1) / 127;
    int32_t rw = 1 + (int32_t)r * (seg - 1) / 127, sy = y + h - (int32_t)s * h / 127;
    int32_t x1 = x + aw, x2 = x1 + dw, x4 = x + w - 1, x3 = x4 - rw;
    cv_under(x, y + h, x1, y, y + h); cv_under(x1, y, x2, sy, y + h);
    cv_under(x2, sy, x3, sy, y + h); cv_under(x3, sy, x4, y + h, y + h);
    cv_hdots(x, x4, y + h, 4);                  /* the floor */
    cv_line(x, y + h, x1, y, c);
    cv_line(x1, y, x2, sy, c);
    cv_line(x2, sy, x3, sy, c);
    cv_line(x3, sy, x4, y + h, c);
    cv_vdots(x1, y, y + h, 3);                  /* the stages' edges */
    cv_vdots(x2, sy, y + h, 3);
    cv_vdots(x3, sy, y + h, 3);
}

/* ---- the graphics, one per kind of page */
static const int32_t BOX_X[NOP] = {6, 88, 170};  /* carrier, mod 1, mod 2: right to left is the signal's way */
#define BOX_W 64
#define BOX_H 62
#define BOX_Y 18

static void gfx_osc(uint32_t o, uint32_t lit)   /* the sketch: carrier <- mod 1 <- mod 2 */
{
    uint32_t k, i;
    for (k = 0; k < NOP; k++) {                 /* the boxes: each operator's wave as the engine plays it */
        static const uint8_t LITS[NOP] = {LIT_C, LIT_M1, LIT_M2};
        const int16_t *t = op_cur[o][k];
        uint16_t edge = lit & LITS[k] ? C_ACC : C_BOX, line = lit & LITS[k] ? C_BRIGHT : C_MID;
        int32_t x0 = BOX_X[k], px = 0, py = 0;
        char s[16];
        static const char *const ROLE[NOP] = {"CAR", "MOD1", "MOD2"};
        cv_text(x0 + (BOX_W - text_w(&FONT_S, ROLE[k])) / 2, 0, &FONT_S, ROLE[k], lit & LITS[k] ? C_MID : C_FAINT);
        cv_frame(x0, BOX_Y, BOX_W, BOX_H, edge);
        cv_hdots(x0 + 3, x0 + BOX_W - 3, BOX_Y + BOX_H / 2, 4);   /* the zero line */
        for (i = 0; t && i < (uint32_t)BOX_W - 6u; i++) {   /* the wave, filled to its zero line */
            int32_t x = x0 + 3 + (int32_t)i, y = BOX_Y + BOX_H / 2 - t[i * PW_N / ((uint32_t)BOX_W - 6u)] * (BOX_H / 2 - 5) / 32767;
            if (i) { cv_under(px, py, x, y, BOX_Y + BOX_H / 2); cv_line(px, py, x, y, line); }
            px = x; py = y;
        }
        put(s, PW_WAVE_NAME[edit.osc[o].bank[k] < PW_BANKS ? edit.osc[o].bank[k] : 0u][wave_index(edit.osc[o].pos[k])]);
        cv_text(x0 + (BOX_W - text_w(&FONT_S, s)) / 2, BOX_Y + BOX_H + 3, &FONT_S, s, lit & LITS[k] ? C_BRIGHT : C_MID);
        if (k) {                                /* the modulator's AR under it: attack up, hold, release down */
            int32_t aw = 1 + (int32_t)edit.osc[o].att[k - 1u] * 18 / 127, rw = 1 + (int32_t)edit.osc[o].rel[k - 1u] * 18 / 127;
            int32_t xa = x0 + 4, xh = xa + aw, xr = x0 + BOX_W - 4 - rw, yb = G_H - 2, yt = BOX_Y + BOX_H + 23;
            uint16_t ac = lit & LIT_AR ? C_BRIGHT : C_FAINT;
            if (lit & LIT_AR) { cv_under(xa, yb, xh, yt, yb); cv_under(xh, yt, xr, yt, yb); cv_under(xr, yt, xr + rw, yb, yb); }
            cv_line(xa, yb, xh, yt, ac);
            cv_line(xh, yt, xr, yt, ac);
            cv_line(xr, yt, xr + rw, yb, ac);
        }
    }
    for (k = 0; k < 2u; k++) {                  /* the arrows: mod k+1 into the one on its left; thicker = more */
        int32_t xr = BOX_X[k + 1u] - 2, xl = BOX_X[k] + BOX_W + 1, y = BOX_Y + BOX_H / 2, mx = (xl + xr) / 2;
        uint32_t amt = edit.osc[o].amt[k], mode = edit.osc[o].mode[k], th = amt > 85u ? 2u : amt > 42u ? 1u : 0u, j;
        uint16_t c = lit & (k ? LIT_A2 : LIT_A1) ? C_ACC : amt ? C_MID : C_FAINT;
        for (j = 0; j <= th; j++) {
            cv_line(xl, y - (int32_t)j, xr, y - (int32_t)j, c);
            cv_line(xl, y + (int32_t)j, xr, y + (int32_t)j, c);
        }
        cv_line(xl, y, xl + 5, y - 5, c);
        cv_line(xl, y, xl + 5, y + 5, c);
        cv_rect(mx - 6, y - 6, 13, 13, C_BLACK); /* the mode, on the arrow */
        if (mode == MODE_FM)
            for (j = 0; j < 11u; j++) {         /* a little sine: frequency modulation */
                static const int8_t SW[11] = {0, 2, 4, 4, 2, 0, -2, -4, -4, -2, 0};
                cv_pset(mx - 5 + (int32_t)j, y - SW[j], c);
                cv_pset(mx - 5 + (int32_t)j, y - SW[j] + 1, c);
            }
        else {
            cv_circle(mx, y, 6, c, 0);          /* ring: a times sign; mix: a plus */
            if (mode == MODE_RING) { cv_line(mx - 3, y - 3, mx + 3, y + 3, c); cv_line(mx - 3, y + 3, mx + 3, y - 3, c); }
            else { cv_line(mx - 3, y, mx + 3, y, c); cv_line(mx, y - 3, mx, y + 3, c); }
        }
    }
}

static int32_t msb64(uint64_t v)                /* the highest set bit (no libgcc: no __builtin_clzll) */
{
    int32_t n = 0;
    while (v >> 1) { v >>= 1; n++; }
    return n;
}

static uint64_t x2_at(int32_t dq)                /* (f / fc)^2 in Q12, dq quarter semitones from the cutoff */
{
    int32_t e = dq >= 0 ? dq / 24 : -((-dq + 23) / 24), f = dq - 24 * e;
    uint64_t x2 = PR_EXP2_24_Q12[f];
    if (e >= 12) return 1u << 24;
    if (e >= 0) return x2 << e;
    return -e >= 31 ? 0 : x2 >> -e;
}

static int32_t lg2_q8(uint64_t v)               /* log2(v) in Q8 (a straight line between the powers of 2) */
{
    int32_t l2 = msb64(v) * 256;
    return l2 + (int32_t)((l2 >= 8 * 256 ? v >> (l2 / 256 - 8) : v << (8 - l2 / 256)) & 255u);
}

static void gfx_filter(uint32_t lit)
{
    /* the response: |H|^2 = 1 / ((1 - x^2)^2 + k^2 x^2), x = f / fc (LP 24: and 1 / (1 + x^4); the comb: its peaks),
     * over MIDI 16 .. 135, +24 .. -36 dB; with the high-pass, each of its one-poles x^2 / (1 + x^2) */
    const int32_t W = 164, top = 4, h = G_H - 12;   /* 2 px a dB */
    uint64_t k12 = eg.f_k >> 12;
    int32_t px, py = 0, fq = eg.flt_type >= FLT_COMBP ? eg.comb_p16 >> 2 : eg.f_cut, cx = (fq - 64) * (W - 1) / 476, hq = edit.hp_cut ? hp_q(edit.hp_cut) : 0;
    uint16_t c = lit & LIT_CURVE ? C_ACC : C_MID;
    cv_hdots(0, W, top + 48, 4);                /* 0 dB, dotted; the octaves; the cutoff */
    for (px = 48; px < 476; px += 48)
        cv_vdots(px * (W - 1) / 476, top, top + h, 6);
    cv_vdots(cx, top, top + h, 2);
    cv_text(4, top, &FONT_S, FLT_NAME[eg.flt_type < FLT_N ? eg.flt_type : 0u], C_FAINT);
    for (px = 0; px < W; px++) {
        int32_t q = 64 + px * 476 / (W - 1), db, y;
        uint64_t x2 = x2_at(q - eg.f_cut), den, one = 4096;
        if (eg.flt_type >= FLT_COMBP) {         /* the comb: |H|^2 = in^2 / (1 + fb^2 - 2 fb cos(2 pi f / f0)) */
            uint32_t ph = (uint32_t)x2_at((q - fq) / 2) << 20;   /* f / f0 (Q12) -> a phase */
            int64_t fb = eg.comb_fb, cs = lfo_wave(LFO_SINE, ph + 0x40000000u, 0);
            den = (uint64_t)((1LL << 30) + fb * fb - 2 * fb * cs);
            db = ((lg2_q8((uint64_t)eg.comb_in * (uint64_t)eg.comb_in) - lg2_q8(den ? den : 1)) * 3083) / (1024 * 256);
        } else {
            den = (x2 > one ? (x2 - one) * (x2 - one) : (one - x2) * (one - x2)) + ((k12 * k12 * x2) >> 12);
            if (!den) den = 1;
            db = (-(lg2_q8(den) - 24 * 256) * 3083) / (1024 * 256);   /* -10 log10(den / 2^24) = -3.0103 log2(...) */
            if (eg.flt_type == FLT_LP24)        /* its first stage: 1 / (1 + x^4) */
                db -= ((lg2_q8(one * one + x2 * x2) - 24 * 256) * 3083) / (1024 * 256);
        }
        if (hq) {                               /* the high-pass: -2 x 10 log10((1 + x^2) / x^2) */
            uint64_t xh = x2_at(q - hq);
            db = xh ? db - 2 * (lg2_q8(xh + one) - lg2_q8(xh)) * 3083 / (1024 * 256) : -99;
        }
        y = top + (24 - db) * 2;
        if (y < top) y = top;
        if (y > top + h - 1) y = top + h - 1;
        if (px) { cv_under(px - 1, py, px, y, top + h); cv_line(px - 1, py, px, y, c); }
        py = y;
    }
    cv_rect(0, G_H - 4, W, 1, C_BOX);           /* drive: a bar under the curve */
    if (edit.drive)
        cv_rect(0, G_H - 5, (int32_t)edit.drive * W / 127, 3, lit & LIT_CURVE ? C_BRIGHT : C_MID);
    cv_vdots(170, 2, G_H - 2, 2);               /* the response | the filter's envelope */
    cv_text(176, 0, &FONT_S, "ENV", C_FAINT);
    cv_adsr(176, 20, 60, G_H - 26, edit.flt_a, edit.flt_d, edit.flt_s, edit.flt_r, lit & LIT_ENV ? C_ACC : C_MID);
}

/* the mod envelope over 2 s (the key held 1.5 s), simulated block by block with the engine's own steps; with LFO
 * RATE or LFO AMT, the LFO under it, as the envelope moves it */
static void gfx_menv(uint32_t lit)
{
    const int32_t x0 = 4, w = 232, top = 4, h = G_H - 10;
    const uint32_t blocks = 2000u * 44100u / 1000u / BLK, held = blocks * 3u / 4u;
    uint32_t b, ph = 0, lfo = eg.me_dest == DEST_LFORATE || eg.me_dest == DEST_LFOAMT, rng = 12345u;
    int32_t px = -1, pe = 0, pl = 0, sh = lfo_random(&rng);
    env_t e = {0, 1};
    for (b = 0; b < blocks; b++) {
        int32_t me = menv_step(&e, b < held) >> 9, x = (int32_t)(b * (uint32_t)w / blocks);
        int32_t mx = (me * eg.me_amt) >> 15, l, ye, yl;
        {
            uint32_t nx = ph + rate_moved(eg.lfo_inc, eg.me_dest == DEST_LFORATE ? (mx * 768) >> 15 : 0);
            if (nx < ph) sh = lfo_random(&rng);  /* S&H: a new value each cycle */
            ph = nx;
        }
        if (x == px)
            continue;
        l = lfo_wave(eg.lfo_wave, ph, sh);
        if (eg.me_dest == DEST_LFOAMT) {
            int32_t f = 32767 + mx;
            l = (l * (f < 0 ? 0 : f)) >> 15;
            l = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        }
        ye = top + h - me * h / 32767;
        yl = top + h / 2 - l * (h / 2 - 2) / 32767;
        if (px >= 0) {
            cv_under(x0 + px, pe, x0 + x, ye, top + h);
            if (lfo) cv_line(x0 + px, pl, x0 + x, yl, lit & LIT_CURVE ? C_BRIGHT : C_FAINT);
            cv_line(x0 + px, pe, x0 + x, ye, lit & LIT_ENV ? C_ACC : C_MID);
        }
        px = x; pe = ye; pl = yl;
    }
    cv_vdots(x0 + w * 3 / 4, top, top + h, 3);   /* the key let go */
    cv_hdots(x0, x0 + w, top + h, 4);
    cv_text(x0 + w * 3 / 4 + 4, top, &FONT_S, "OFF", C_FAINT);
}

static void gfx_lfo(uint32_t lit)              /* the shape: its cycles (more at faster rates), the delay as a fade-in */
{
    if (lit & LIT_MENV) {
        gfx_menv(lit);
        return;
    }
    const int32_t x0 = 4, w = 232, mid = G_H / 2, amp = G_H / 2 - 8;
    uint32_t l2 = lit & LIT_L2, wave = l2 ? edit.lfo2_wave : edit.lfo_wave;   /* (LFO 2: no delay, no retrig) */
    uint32_t rate = l2 ? (edit.lfo2_sync ? 16u + edit.lfo2_sync * 10u : edit.lfo2_rate) : edit.lfo_rate;   /* (faster divisions: more cycles) */
    int32_t fade_w = l2 ? 0 : (int32_t)edit.lfo_delay * 70 / 127, px, py = mid;
    uint32_t cycles = 1u + rate * 7u / 127u, rng = 12345u, wv = wave < LFO_N ? wave : 0u;
    int32_t sh = lfo_random(&rng);
    uint32_t last_ph = 0;
    uint16_t c = lit & LIT_CURVE ? C_ACC : C_MID;
    cv_hdots(x0, x0 + w, mid, 4);               /* the centre line; the cycles */
    for (px = 1; px < (int32_t)cycles; px++)
        cv_vdots(x0 + px * w / (int32_t)cycles, 4, G_H - 4, 6);
    for (px = 0; px < w; px++) {
        uint32_t ph = (uint32_t)((uint64_t)px * cycles * 0x100000000ull / (uint64_t)w);
        int32_t l, y, g = px < fade_w ? px * 32767 / (fade_w ? fade_w : 1) : 32767;
        if (ph < last_ph || px == 0) sh = lfo_random(&rng);   /* S&H: a new value each cycle */
        last_ph = ph;
        l = (lfo_wave(wv, ph, sh) * g) >> 15;
        y = mid - l * amp / 32767;
        if (px) { cv_under(x0 + px - 1, py, x0 + px, y, mid); cv_line(x0 + px - 1, py, x0 + px, y, c); }
        py = y;
    }
    if (fade_w)                                 /* the delay: where it has faded in */
        for (py = 2; py < (int32_t)G_H - 4; py += 3)
            cv_pset(x0 + fade_w, py, C_FAINT);
    if (!l2 && edit.lfo_retrig)                 /* retrig: each note starts it here */
        cv_rect(x0, 2, 2, G_H - 6, lit & LIT_CURVE ? C_ACC : C_MID);
}

static void gfx_fx(uint32_t lit)               /* chorus -> delay -> reverb: a box each, a picture of it, its mix */
{
    static const int32_t X[3] = {4, 84, 164};
    static const char *const NAME[3] = {"CHORUS", "DELAY", "REVERB"};
    const int32_t W = 72, H = 96;
    const uint32_t LITS[3] = {LIT_C, LIT_M1, LIT_M2};
    const uint8_t MIX[3] = {edit.cho_mix, edit.dly_mix, edit.rev_mix};
    uint32_t k;
    int32_t i;
    for (k = 0; k < 3u; k++) {
        uint16_t edge = lit & LITS[k] ? C_ACC : C_BOX;
        int32_t x0 = X[k];
        cv_frame(x0, 0, W, H, edge);
        cv_text(x0 + (W - text_w(&FONT_S, NAME[k])) / 2, H + 4, &FONT_S, NAME[k], lit & LITS[k] ? C_BRIGHT : C_FAINT);
        cv_rect(x0, H + 22, W, 1, C_BOX);       /* its mix */
        cv_rect(x0, H + 21, (int32_t)MIX[k] * W / 127, 3, MIX[k] ? (lit & LITS[k] ? C_BRIGHT : C_MID) : C_FAINT);
        if (k < 2u) {                           /* the arrow on to the next */
            cv_line(x0 + W + 1, H / 2, X[k + 1u] - 2, H / 2, C_FAINT);
            cv_line(X[k + 1u] - 2, H / 2, X[k + 1u] - 5, H / 2 - 3, C_FAINT);
            cv_line(X[k + 1u] - 2, H / 2, X[k + 1u] - 5, H / 2 + 3, C_FAINT);
        }
    }
    {                                           /* the chorus: two lines swept against each other (left, right) */
        int32_t a = 3 + (int32_t)edit.cho_depth * 36 / 127, py0 = 0, py1 = 0;
        uint32_t cyc = 1u + (uint32_t)edit.cho_rate * 3u / 127u;
        for (i = 0; i < W - 8; i++) {
            uint32_t ph = (uint32_t)((uint64_t)i * cyc * 0x100000000ull / (uint64_t)(W - 8));
            int32_t y0 = H / 2 - lfo_wave(LFO_SINE, ph, 0) * a / 32767;
            int32_t y1 = H / 2 - lfo_wave(LFO_SINE, ph + 0x40000000u, 0) * a / 32767;
            if (i) {
                cv_line(X[0] + 3 + i, py0, X[0] + 4 + i, y0, lit & LIT_C ? C_BRIGHT : C_MID);
                cv_line(X[0] + 3 + i, py1, X[0] + 4 + i, y1, lit & LIT_C ? C_ACC : C_FAINT);
            }
            py0 = y0; py1 = y1;
        }
    }
    {                                           /* the delay: the note, then its repeats (time apart, fading by the
                                                 * feedback; ping-pong: alternately up and down; shift: they move) */
        int32_t gap = 4 + (int32_t)edit.dly_time * 28 / 127, h = 84, x = X[1] + 4, n = 0;
        int32_t fb = (int32_t)edit.dly_fb * 31000 / 127;
        while (x < X[1] + W - 3 && h > 1 && n < 20) {
            int32_t up = !edit.dly_ping || !(n & 1), s = n ? ((int32_t)edit.dly_shift - 12) * n / 3 : 0, top;
            int32_t len = h / 2;
            top = up ? H / 2 - len - s : H / 2 + 1 - s;
            if (top < 2) top = 2;
            if (top + len > H - 2) top = H - 2 - len;
            cv_rect(x, top, 3, len, n ? (lit & LIT_M1 ? C_MID : C_FAINT) : (lit & LIT_M1 ? C_BRIGHT : C_MID));
            x += gap;
            if (n++) h = h * fb / 32767;
        }
    }
    {                                           /* the reverb: the pre-delay, then the tail (longer with the size) */
        int32_t pre = (int32_t)edit.rev_pre * 16 / 127, len = 8 + (int32_t)edit.rev_size * (W - 16 - pre) / 127;
        int32_t x0 = X[2] + 4 + pre, px = x0, py = 6;
        uint16_t c = lit & LIT_M2 ? C_ACC : C_MID;
        cv_rect(X[2] + 4, H - 6, 1, 1, c);
        cv_line(X[2] + 4, H - 6, x0, H - 6, C_FAINT);
        cv_line(x0, H - 6, x0, 6, c);
        for (i = 1; i <= len; i++) {            /* an exponential decay, drawn as a line and a few ticks under it */
            int32_t y = 6 + (H - 12) - (H - 12) * (len - i) * (len - i) / (len * len);
            cv_line(px, py, x0 + i, y, c);
            cv_line(x0 + i, y + 1, x0 + i, H - 6, C_FILL);
            px = x0 + i; py = y;
        }
    }
}

/* the arpeggiator: the next 16 steps from the notes held now (as the ISR will play them), each where its swung step
 * falls and as long as its gate, the one playing lit */
static uint32_t arp_drawn = 0xFFFFFFFFu;        /* arp.steps when last drawn */
static void gfx_arp(uint32_t lit)
{
    uint8_t nt[ARP_MAX], vl[ARP_MAX], pat[16];
    uint32_t n = held.n, i, idx = arp.idx, base, rng = 0xC0FFEEu, lo = 127, hi = 0, vel, x = 0, tot = 0;
    uint16_t c = lit & LIT_CURVE ? C_ACC : C_MID;
    arp_drawn = arp.steps;
    if (!edit.arp_on) {
        cv_text(36, 54, &FONT_S, "OFF: KNOB1 TURNS IT ON", C_FAINT);
        return;
    }
    if (n > ARP_MAX) n = ARP_MAX;
    for (i = 0; i < n; i++) { nt[i] = held.note[i]; vl[i] = held.vel[i]; }
    if (!n) {
        cv_text(64, 54, &FONT_S, "HOLD SOME KEYS", C_FAINT);
        return;
    }
    base = idx ? (idx - 1u) / 16u * 16u : 0u;   /* the 16 the playing step is in */
    for (i = 0; i < 16u; i++) {
        pat[i] = (uint8_t)arp_pick(nt, vl, n, base + i, &vel, &rng);
        if (pat[i] < lo) lo = pat[i];
        if (pat[i] > hi) hi = pat[i];
    }
    for (i = 0; i < 16u; i++)                   /* (the swung lengths, scaled to the width) */
        tot += arp_step_len(i & 1u) >> 8;
    for (i = 0; i < 16u; i++) {
        uint32_t w = (arp_step_len(i & 1u) >> 8) * 236u / tot, gw = edit.arp_gate >= 127u ? w : w * (5u + edit.arp_gate * 95u / 127u) / 100u;
        int32_t y = hi > lo ? 104 - (int32_t)((pat[i] - lo) * 96u / (hi - lo)) : 56;
        uint32_t playing = idx && i == (idx - 1u) - base;
        if (!(i & 3u)) cv_vdots(2 + (int32_t)x, 4, G_H - 8, 4);   /* the beats */
        cv_rect(2 + (int32_t)x, y, gw > 1u ? (int32_t)gw - 1 : 1, 8, playing ? C_BRIGHT : c);
        cv_rect(2 + (int32_t)x, G_H - 4, (int32_t)w - 1, 2, playing ? C_BRIGHT : C_BOX);
        x += w;
    }
}

static void gfx_setup(void)                     /* the MIDI setup, in words */
{
    char s[40], *e;
    e = put(s, "MIDI IN ");
    if (gset.midi_in) e = num(put(e, "CH "), gset.midi_in); else e = put(e, "OMNI");
    put(num(put(e, "   OUT CH "), gset.midi_out + 1u), "");
    cv_text(8, 6, &FONT_S, s, C_MID);
    e = put(s, gset.clock ? "ARP CLOCK: MIDI " : "ARP CLOCK: ITS OWN");
    if (gset.clock) {
        uint32_t b = mc_bpm10;
        if (b) { e = num(e, b / 10u); *e++ = '.'; put(num(e, b % 10u), " BPM"); }
        else put(e, "(NONE YET)");
    }
    cv_text(8, 28, &FONT_S, s, gset.clock ? C_ACC : C_MID);
    e = put(s, "PITCH BEND ");
    if (gset.bend) put(num(put(e, "+-"), gset.bend), " SEMITONES"); else put(e, "OFF");
    cv_text(8, 50, &FONT_S, s, C_MID);
    {                                           /* the battery: its bars, and the reading (stock thresholds 531 / 561 / 591) */
        uint32_t b = fm1_batt();
        int32_t x;
        cv_batt(8, 76, b, 1);
        e = b == 4u ? put(s, "USB POWER") : b == 0u ? put(s, "BATTERY LOW") : num(put(s, "BATTERY "), b);
        if (b && b < 4u) put(e, "/3");
        x = cv_text(32, 72, &FONT_S, s, b == 0u ? C_BRIGHT : C_MID);
        e = num(put(s, "ADC "), (uint32_t)(fm1_batt_raw > 0 ? fm1_batt_raw : 0));
        cv_text(x + 12, 72, &FONT_S, s, C_FAINT);
    }
    cv_rect(8, 98, 224, 1, C_BOX);
    cv_text(8, 104, &FONT_S, "GLOBAL: NOT PART OF A PATCH", C_FAINT);
}

/* the sequencer: 64 squares, 16 a row: a rest empty, a note grey, an accent white, a slide joined to the next;
 * past the length a dot; the cursor ringed (doubled while REC), the step playing marked under */
static uint32_t seq_drawn = 0xFFFFFFFFu;
static void gfx_seq(uint32_t lit)
{
    uint32_t k, len = edit.seq_len >= 1u && edit.seq_len <= 64u ? edit.seq_len : 16u;
    (void)lit;
    seq_drawn = seq.steps;
    for (k = 0; k < 64u; k++) {
        int32_t x = 2 + (int32_t)(k % 16u) * 15, y = 3 + (int32_t)(k / 16u) * 30, gx = (int32_t)(k % 16u);
        uint32_t tr = edit.seq_trig[k];
        if (gx == 4 || gx == 8 || gx == 12)     /* the beats */
            cv_vdots(x - 2, y, y + 22, 2);
        if (k >= len) {
            cv_rect(x + 5, y + 10, 2, 2, C_BOX);
            continue;
        }
        if (tr == SEQ_REST)
            cv_frame(x, y, 12, 22, C_BOX);
        else {
            cv_rect(x, y, 12, 22, tr == SEQ_ACCENT ? C_BRIGHT : C_MID);
            if (edit.seq_slide[k] && k + 1u < len && (k % 16u) != 15u)
                cv_rect(x + 12, y + 10, 3, 3, tr == SEQ_ACCENT ? C_BRIGHT : C_MID);   /* the slide: on to the next */
        }
        if (k == seq_cursor) {                  /* the cursor */
            uint16_t c = C_BRIGHT;
            cv_frame(x - 2, y - 2, 16, 26, c);
            if (seq_rec) cv_frame(x - 1, y - 1, 14, 24, c);
        }
        if (eg.seq_play && k == seq.playing)    /* the step playing */
            cv_rect(x, y + 24, 12, 2, C_BRIGHT);
    }
}

static void gfx_mix(void)
{
    char s[24], *e;
    cv_text(4, 8, &FONT_S, "OSC1", C_MID);
    cv_rect(48, 13, 186, 6, C_FILL);
    cv_rect(48, 13, (int32_t)edit.lvl1 * 186 / 127, 6, C_BRIGHT);
    cv_text(4, 36, &FONT_S, "OSC2", C_MID);
    cv_rect(48, 41, 186, 6, C_FILL);
    cv_rect(48, 41, (int32_t)edit.lvl2 * 186 / 127, 6, C_BRIGHT);
    e = put(s, "OSC2 ");
    e = put(snum(e, (int32_t)edit.semi2 - 24), "st ");
    put(snum(e, ((int32_t)edit.fine2 - 64) * 50 / 63), "c");
    cv_text(48, 64, &FONT_S, s, C_MID);
    cv_rect(4, 90, 232, 1, C_BOX);
    e = put(s, "GLIDE ");
    if (edit.glide) put(e, edit.glide_mode == GLIDE_LEGATO ? "LEGATO" : "ALWAYS"); else put(e, "OFF");
    cv_text(4, 98, &FONT_S, s, edit.glide ? C_MID : C_FAINT);
}

static char pop_name[16], pop_val[20];          /* the knob just turned (or PRESETS: the cutoff) */
static uint32_t pop_until, pop_drawn;

static void pop(const char *name, const char *val)
{
    put(pop_name, name);
    put(pop_val, val);
    pop_until = fm1_ms + POP_MS;
    dirty |= D_GFX;
}

static void draw_gfx(void)
{
    uint32_t lit = PAGES[page].rows[row[page]].lit;
    cv_begin(240, G_H, C_BLACK);
    switch (page) {
    case PG_OSC1: gfx_osc(0, lit); break;
    case PG_OSC2: gfx_osc(1, lit); break;
    case PG_FILTER: gfx_filter(lit); break;
    case PG_AMP: cv_adsr(8, 6, 224, G_H - 14, edit.amp_a, edit.amp_d, edit.amp_s, edit.amp_r, C_ACC); break;
    case PG_LFO: gfx_lfo(lit); break;
    case PG_LFO2: gfx_lfo(lit | LIT_L2); break;
    case PG_CHO: case PG_DLY: case PG_REV: gfx_fx(lit); break;
    case PG_ARP: gfx_arp(lit); break;
    case PG_SETUP: gfx_setup(); break;
    case PG_SEQ: gfx_seq(lit); break;
    default: gfx_mix();
    }
    if (fm1_ms < pop_until) {                   /* a knob turned: its name and value, large, in the top right corner */
        const felucca_font_t *f = text_w(&FONT_P, pop_val) <= 220 ? &FONT_P : &FONT_M;
        int32_t w = text_w(f, pop_val), nw = text_w(&FONT_S, pop_name), bw = (w > nw ? w : nw) + 16, x0, h = 24 + f->h;
        bw = bw < 80 ? 80 : bw > 236 ? 236 : bw;
        x0 = 238 - bw;
        cv_rect(x0, 0, bw, h, C_BLACK);         /* (the picture shows around it) */
        cv_frame(x0, 0, bw, h, C_MID);
        cv_text(x0 + (bw - nw) / 2, 3, &FONT_S, pop_name, C_MID);
        cv_text(x0 + (bw - w) / 2, 20, f, pop_val, C_BRIGHT);
        pop_drawn = 1;
    } else
        pop_drawn = 0;
    cv_blit(0, G_Y);
}

#define PG_PATCH PG_N                           /* (the PATCH screen: not in ALGORITHM's round, only SAVE leads here) */
enum { PA_SAVE, PA_LOAD, PA_INIT, PA_N };
static const char *const PA_NAME[PA_N] = {"SAVE", "LOAD", "INIT"};
static uint32_t pa_row;

/* ---- the header, the focus row, the tabs */
static void draw_hdr(void)
{
    char s[32], *e;
    int32_t x;
    uint32_t cpu = 0;
    cv_begin(240, 20, C_BLACK);
    x = cv_text(4, 1, &FONT_S, page < PG_N ? PAGES[page].name : "PATCH", C_BRIGHT);
    if (page == PG_SEQ && seq_rec) {            /* REC: inverted */
        cv_rect(x + 6, 1, 36, 16, C_BRIGHT);
        x = cv_text(x + 10, 1, &FONT_S, "REC", C_BLACK) + 4;
    }
    cv_line(x + 6, 5, x + 9, 9, C_FAINT); cv_line(x + 9, 9, x + 6, 13, C_FAINT);   /* > the row */
    cv_text(x + 15, 1, &FONT_S, page < PG_N ? PAGES[page].rows[row[page]].name : PA_NAME[pa_row], C_MID);
    e = s;                                      /* the slot, and * for unsaved changes */
    if (cur_slot >= 0) e = num(e, (uint32_t)cur_slot + 1u);
    else e = put(e, "-");
    if (modified()) e = put(e, "*");
    cv_text(176 - text_w(&FONT_S, s), 1, &FONT_S, s, modified() ? C_BRIGHT : C_MID);
    cv_batt(181, 5, fm1_batt(), 1);             /* the battery, before the CPU */
#ifndef DINGHY_HOST
    cpu = cpu_max * 100u / RENDER_TICKS;        /* the longest render since the last header: the peak */
    cpu_max = 0;
#endif
    cv_rect(203, 5, 33, 9, C_BOX);              /* the CPU: a bar, bright past 70 % */
    cv_rect(204, 6, 31, 7, C_BLACK);
    cv_rect(205, 7, (int32_t)(cpu > 100u ? 100u : cpu) * 29 / 100, 5, cpu > 70u ? C_BRIGHT : C_MID);
    cv_rect(0, 19, 240, 1, C_BOX);              /* the rule under it */
    cv_blit(0, HDR_Y);
}

static void draw_focus(void)                    /* the selected row: four labels, values (large), bars */
{
    const row_t *r = &PAGES[page].rows[row[page]];
    uint32_t i;
    cv_begin(240, FOC_H, C_BLACK);
    for (i = 0; i < 4u; i++) {
        const prm_t *p = &r->p[i];
        const uint8_t *v;
        char s[16];
        int32_t x = (int32_t)i * FOC_W, w;
        if (p->kind == K_NONE)
            continue;
        v = pval(page, p);
        fmt(s, p, v);
        cv_text(x + (FOC_W - text_w(&FONT_S, p->label)) / 2, 0, &FONT_S, p->label, C_MID);
        if ((w = text_w(&FONT_M, s)) <= FOC_W - 4)
            cv_text(x + (FOC_W - w) / 2, 19, &FONT_M, s, C_BRIGHT);
        else                                    /* (too long for the large font: the small one) */
            cv_text(x + (FOC_W - text_w(&FONT_S, s)) / 2, 22, &FONT_S, s, C_BRIGHT);
        if (p->kind == K_NUM || p->kind == K_POS || p->kind == K_BIP || p->kind == K_CENT) {
            int32_t bx = x + 6, bw = FOC_W - 12, by = 46;
            cv_rect(bx, by + 1, bw, 1, C_BOX);  /* the track */
            if (p->kind == K_BIP || p->kind == K_CENT) {   /* bipolar: from the middle */
                int32_t m = bx + bw / 2, d = ((int32_t)*v - 64) * (bw / 2) / 63;
                cv_rect(m, by - 1, 1, 5, C_FAINT);
                cv_rect(d < 0 ? m + d : m, by, d < 0 ? -d : d + 1, 3, C_BRIGHT);
            } else
                cv_rect(bx, by, (int32_t)*v * bw / p->max, 3, C_BRIGHT);
        }
    }
    cv_blit(0, FOC_Y);
}

static void tabs(const char *const *name, uint32_t nn, uint32_t sel)   /* names along the bottom, sel inverted */
{
    int32_t xs[8], ws[8], x = 0, shift = 0;
    uint32_t n;
    for (n = 0; n < nn && n < 8u; n++) {        /* where each goes; scrolled to keep the selected one on screen */
        ws[n] = text_w(&FONT_S, name[n]) + 10;
        xs[n] = x;
        x += ws[n] + 4;
    }
    if (xs[sel] + ws[sel] > 238) shift = xs[sel] + ws[sel] - 238;
    cv_begin(240, TAB_H, C_BLACK);
    cv_rect(0, 0, 240, 1, C_BOX);               /* the rule over them */
    for (n = 0; n < nn && n < 8u; n++) {
        int32_t tx = xs[n] - shift + (x - 4 - shift < 240 ? (240 - (x - 4)) / 2 : 1);   /* (centred when they fit) */
        if (n == sel) {
            cv_rect(tx, 4, ws[n], 17, C_BRIGHT);
            cv_text(tx + 5, 5, &FONT_S, name[n], C_BLACK);
        } else
            cv_text(tx + 5, 5, &FONT_S, name[n], C_MID);
    }
    cv_blit(0, TAB_Y);
}

static void draw_tabs(void)                     /* every row of the page */
{
    const char *name[8];
    uint32_t n;
    for (n = 0; n < PAGES[page].n && n < 8u; n++)
        name[n] = PAGES[page].rows[n].name;
    tabs(name, n, row[page]);
}

/* ---- the PATCH screen (the SAVE button): the 8 slots as tiles, then SAVE / LOAD / INIT */
static uint32_t pa_slot, pa_armed, pa_back = PG_OSC1;
static const char *pa_msg = "";
static char pa_msgbuf[32];

static int32_t wave_sample(uint32_t b, uint32_t pos, uint32_t i)   /* a morphed wave, from flash (the thumbnails) */
{
    uint32_t q = (pos & 127u) * (PW_WAVES - 1u) * 256u / 127u, w = q >> 8, f = q & 255u;
    if (b >= PW_BANKS) b = 0;
    if (w >= PW_WAVES - 1u) { w = PW_WAVES - 2u; f = 256u; }
    return PW_WAVE[b][w][i] + (((int32_t)PW_WAVE[b][w + 1u][i] - PW_WAVE[b][w][i]) * (int32_t)f >> 8);
}

static void pa_draw_gfx(void)                   /* the 8 slots as tiles: the number, osc 1's carrier as a thumbnail */
{
    uint32_t s, i;
    cv_begin(240, G_H, C_BLACK);
    for (s = 0; s < PATCH_SLOTS; s++) {
        int32_t x = 2 + (int32_t)(s % 4u) * 60, y = 2 + (int32_t)(s / 4u) * 62, w = 56, h = 58, px = 0, py = 0;
        int32_t mid = y + 34, sel = s == pa_slot;
        char n[4];
        cv_frame(x, y, w, h, sel ? C_BRIGHT : C_BOX);
        if (sel) cv_frame(x + 1, y + 1, w - 2, h - 2, C_BRIGHT);   /* the selected slot: a double edge */
        num(n, s + 1u);
        cv_text(x + 5, y + 3, &FONT_S, n, bank.used[s] ? C_BRIGHT : C_FAINT);
        if ((int32_t)s == cur_slot)             /* where the sound came from */
            cv_circle(x + w - 8, y + 10, 3, C_BRIGHT, 1);
        if (bank.used[s]) {                     /* its sound: osc 1's carrier */
            cv_hdots(x + 5, x + w - 5, mid, 4);
            for (i = 0; i < 44u; i++) {
                int32_t xx = x + 6 + (int32_t)i, yy = mid -
                             wave_sample(bank.p[s].osc[0].bank[0], bank.p[s].osc[0].pos[0], i * PW_N / 44u) * 16 / 32767;
                if (i) { cv_under(px, py, xx, yy, mid); cv_line(px, py, xx, yy, sel ? C_BRIGHT : C_MID); }
                px = xx; py = yy;
            }
        } else
            cv_text(x + (w - text_w(&FONT_S, "EMPTY")) / 2, mid - 8, &FONT_S, "EMPTY", C_FAINT);
    }
    cv_blit(0, G_Y);
}

static void pa_draw_text(void)                  /* the header, what SAVE will do, the message, the tabs */
{
    char s[40], *e, n[4];
    const char *m = pa_msg;
    num(n, pa_slot + 1u);
    draw_hdr();
    cv_begin(240, FOC_H, C_BLACK);
    e = pa_row == PA_SAVE ? put(s, "SAVE TO SLOT ") : pa_row == PA_LOAD ? put(s, "LOAD SLOT ") : put(s, "LOAD THE DEFAULT SOUND");
    if (pa_row != PA_INIT) {
        e = put(e, n);
        if (pa_row == PA_LOAD && !bank.used[pa_slot]) put(e, " (EMPTY)");
        if (pa_row == PA_SAVE && bank.used[pa_slot]) put(e, " (USED)");
    }
    cv_text((240 - text_w(&FONT_S, s)) / 2, 0, &FONT_S, s, C_MID);
    if (*m)                                     /* what happened, or what to do */
        cv_text((240 - text_w(&FONT_S, m)) / 2, 24, &FONT_S, m, C_BRIGHT);
    else
        cv_text((240 - text_w(&FONT_S, "KNOB1 SLOT  SAVE DO IT")) / 2, 24, &FONT_S, "KNOB1 SLOT  SAVE DO IT", C_FAINT);
    if (pa_armed) cv_frame(4, 20, 232, 24, C_BRIGHT);   /* (asking again: framed) */
    cv_blit(0, FOC_Y);
    tabs(PA_NAME, PA_N, pa_row);
}

static void pa_say(const char *a, uint32_t slot)   /* the message line: a, then the slot's number if slot > 0 */
{
    char *e = put(pa_msgbuf, a);
    if (slot) num(e, slot);
    pa_msg = pa_msgbuf;
}

static int pa_do(void)                          /* the SAVE button on the PATCH screen; 1 = the sound changed */
{
    patch_t p;
    if (pa_row == PA_SAVE) {
        if (bank.used[pa_slot] && (int32_t)pa_slot != cur_slot && !pa_armed) {
            pa_armed = 1;
            pa_say("SLOT USED: SAVE AGAIN", 0);
            return 0;
        }
        pa_armed = 0;
        if (patch_save(pa_slot, &edit) != 0) { pa_say("SAVE FAILED", 0); return 0; }
        clean = edit;
        cur_slot = (int32_t)pa_slot;
        slot_moved = 1;
        pa_say("SAVED TO ", pa_slot + 1u);
        return 0;
    }
    if (pa_row == PA_LOAD && patch_get(pa_slot, &p) != 0) {
        put(num(put(pa_msgbuf, "SLOT "), pa_slot + 1u), " IS EMPTY");
        pa_msg = pa_msgbuf;
        return 0;
    }
    if (modified() && !pa_armed) {              /* LOAD / INIT over unsaved changes: ask once more */
        pa_armed = 1;
        pa_say("UNSAVED: SAVE AGAIN", 0);
        return 0;
    }
    pa_armed = 0;
    if (pa_row == PA_LOAD) {
        edit = p;
        cur_slot = (int32_t)pa_slot;
        pa_say("LOADED ", pa_slot + 1u);
    } else {
        edit = PATCH_INIT;
        cur_slot = -1;
        pa_say("DEFAULT SOUND LOADED", 0);
    }
    clean = edit;
    slot_moved = 1;
    return 1;
}

static int pa_input(uint32_t buttons)           /* the PATCH screen's encoders and its SAVE; 1 = the sound changed */
{
    int32_t d;
    uint32_t i, changed = 0;
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) {
        int32_t n = (int32_t)pa_row + d;
        pa_row = (uint32_t)(n < 0 ? 0 : n >= PA_N ? PA_N - 1 : n);
        pa_armed = 0;
        pa_msg = "";
        dirty |= D_ALL;
    }
    for (i = 0; i < 4u; i++)                    /* KNOB1: the slot (the others do nothing here) */
        if ((d = fm1_enc_take(ENC_KNOB1 + i)) != 0 && i == 0) {
            pa_slot = (uint32_t)(((int32_t)pa_slot + d % 8 + 8) % 8);
            pa_armed = 0;
            pa_msg = "";
            dirty |= D_ALL;
        }
    (void)fm1_enc_take(ENC_PRESETS);
    if (buttons & (1u << BTN_SAVE)) {
        changed = (uint32_t)pa_do();
        dirty |= D_ALL;
    }
    return (int)changed;
}

/* ---- the SCOPE (the HOME button): the output, left (white) and right (grey), triggered on a rising zero, over
 * sc_cycles periods of the newest note (whatever the note, the wave stands still), redrawn every SC_FRAME_MS */
#define PG_SCOPE (PG_N + 1u)
#define SC_H 132
#define SC_Y 40u
#define SC_FRAME_MS 40u
static uint32_t sc_back = PG_OSC1, sc_cycles = 2, sc_gain = 1, sc_shown = 0xFFFF;   /* gain: x1 .. x8 (auto) */

static uint32_t sc_note(void)                   /* the newest sounding note (255: none) */
{
    uint32_t v, best = 0, note = 255;
    for (v = 0; v < NV; v++)
        if (voice[v].amp.st && voice[v].age >= best) { best = voice[v].age; note = voice[v].note; }
    return note;
}

static uint32_t sc_period(void)                 /* the newest note's period in frames (0: nothing sounds) */
{
    uint32_t note = sc_note(), inc;
    if (note > 127u) return 0;
    inc = INC_C9[note % 12u] >> (10u - note / 12u);
    return inc ? 0xFFFFFFFFu / inc : 0;
}

static void sc_draw(void)
{
    static int16_t snap[2][SCOPE_N];
    uint32_t end = scope_w, p = sc_period(), w, s, k, ch, i;
    int32_t x, py[2] = {0, 0};
    w = p ? p * sc_cycles : 512u;               /* the window */
    if (w < 32u) w = 32u;
    if (w > SCOPE_N / 2u) w = SCOPE_N / 2u;
    for (i = 0; i < SCOPE_N; i++) {             /* a snapshot: the ISR keeps writing */
        snap[0][i] = scope_buf[0][i];
        snap[1][i] = scope_buf[1][i];
    }
    s = end - w - 1u;                           /* the trigger: the latest rising zero with a window after it */
    for (k = 0; k < (p ? p : 256u) + 1u && k < SCOPE_N / 2u - 2u; k++, s--)
        if (snap[0][(s - 1u) & (SCOPE_N - 1u)] < 0 && snap[0][s & (SCOPE_N - 1u)] >= 0)
            break;
    {                                           /* auto gain: the window's peak to nine tenths of the height, x1 .. x8 */
        int32_t pk = 1;
        for (x = 0; x < 240; x++)
            for (ch = 0; ch < 2u; ch++) {
                int32_t v = snap[ch][(s + (uint32_t)x * w / 240u) & (SCOPE_N - 1u)];
                if (v < 0) v = -v;
                if (v > pk) pk = v;
            }
        sc_gain = (uint32_t)(29490 / pk);
        sc_gain = sc_gain < 1u ? 1u : sc_gain > 8u ? 8u : sc_gain;
    }
    cv_begin(240, SC_H, C_BLACK);
    for (x = 0; x < 240; x += 4) {              /* the graticule: the centre, the quarters, the cycles */
        cv_pset(x, SC_H / 2, C_BOX);
        cv_pset(x, SC_H / 4, C_SEL);
        cv_pset(x, 3 * SC_H / 4, C_SEL);
    }
    for (k = 1; p && k < sc_cycles; k++)
        for (i = 0; i < (uint32_t)SC_H; i += 4u)
            cv_pset((int32_t)(k * 240u / sc_cycles), (int32_t)i, C_BOX);
    for (ch = 2; ch-- > 0;)                     /* right under, left over */
        for (x = 0; x < 240; x++) {
            int32_t v = snap[ch][(s + (uint32_t)x * w / 240u) & (SCOPE_N - 1u)] * (int32_t)sc_gain;
            int32_t y = SC_H / 2 - v * (SC_H / 2 - 2) / 32767;
            if (x) cv_line(x - 1, py[ch], x, y, ch ? C_MID : C_BRIGHT);
            py[ch] = y;
        }
    cv_blit(0, SC_Y);
}

static void sc_text(void)                       /* the header: the note, the cycles; the zoom; the hints */
{
    char s[32], *e;
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t note = sc_note();
    sc_shown = note << 8 | sc_cycles << 4 | sc_gain;
    draw_text_box(4, HDR_Y, 130, &FONT_S, "SCOPE", C_ACC, 0);
    e = s;
    if (note < 128u) {
        e = put(e, NAME[note % 12u]);
        e = num(e, note / 12u - 1u);
        e = put(e, "  ");
    }
    e = num(e, sc_cycles);
    put(e, sc_cycles == 1u ? " CYCLE" : " CYCLES");
    draw_text_box(110, HDR_Y, 126, &FONT_S, s, C_FAINT, 2);
    e = put(s, "L WHITE   R GREY   x");
    num(e, sc_gain);
    draw_text_box(0, SC_Y + SC_H + 12u, 240, &FONT_S, s, C_FAINT, 1);
    draw_text_box(0, 222, 240, &FONT_S, "KNOB1: CYCLES   HOME: BACK", C_FAINT, 1);
}

static void ui_page(uint32_t pg)
{
    if (pg > PG_SCOPE) return;
    if (pg != page) lcd_fill(0, G_Y, 240, 240 - G_Y, C_BLACK);   /* (row counts differ: clear what was below) */
    if (pg == PG_SCOPE && page != PG_SCOPE)    /* in: from where HOME was pressed */
        sc_back = page == PG_PATCH ? pa_back : page;
    scope_on = pg == PG_SCOPE;                  /* (the ISR fills the capture only while it is shown) */
    if (pg == PG_PATCH && page != PG_PATCH) {   /* in: from where SAVE was pressed, the slot of the sound */
        pa_back = page;
        pa_armed = 0;
        pa_msg = "";
        pa_row = PA_SAVE;
        if (cur_slot >= 0) pa_slot = (uint32_t)cur_slot;
    }
    page = pg;
    pop_until = 0;                              /* (the last page's pop-up goes with it) */
    dirty = D_ALL;
}

/* ---- input: the encoders and the page buttons; 1 if the sound changed */
static int ui_input(uint32_t buttons)
{
    static uint32_t last_turn[4];
    static int32_t slow[4];
    const row_t *r;
    int32_t d;
    uint32_t i, changed = 0;
    if (buttons & (1u << BTN_PLAY)) {           /* PLAY: the sequencer runs / stops, from anywhere */
        engine_seq_play(!eg.seq_play);
        dirty |= D_GFX;
    }
    if (buttons & (1u << BTN_REC)) {            /* REC: step entry (and to the SEQ page) */
        seq_rec = !seq_rec;
        if (page != PG_SEQ) ui_page(PG_SEQ);
        dirty |= D_ALL;
        return (int)changed;
    }
    if (page != PG_PATCH && (d = fm1_enc_take(ENC_PRESETS)) != 0) {   /* PRESETS: the cutoff, from any page */
        static uint32_t last_cut;
        int32_t x = (int32_t)edit.cut + (fm1_ms - last_cut < 60u ? d * 3 : d);   /* (fast: x 3) */
        last_cut = fm1_ms;
        x = x < 0 ? 0 : x > 127 ? 127 : x;
        if (x != edit.cut) {
            edit.cut = (uint8_t)x;
            changed = 1;
            {
                char val[8];
                num(val, (uint32_t)x);
                pop("CUTOFF", val);
            }
            dirty |= D_HDR | (page == PG_FILTER ? D_GFX | D_ROWS : 0u);
        }
    }
    if (page == PG_SCOPE) {                     /* the SCOPE: KNOB1 the cycles; HOME, ALGORITHM, SAVE or a page leave */
        if ((d = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t c = (int32_t)sc_cycles + d;
            sc_cycles = (uint32_t)(c < 1 ? 1 : c > 8 ? 8 : c);
            dirty |= D_ALL;
        }
        for (i = 1; i < 4u; i++) (void)fm1_enc_take(ENC_KNOB1 + i);
        (void)fm1_enc_take(ENC_SELECT);
        if (buttons & (1u << BTN_HOME) || fm1_enc_take(ENC_ALGORITHM)) { ui_page(sc_back); return (int)changed; }
        if (buttons & (1u << BTN_SAVE)) { page = sc_back; ui_page(PG_PATCH); return (int)changed; }
        if (!(buttons & (1u << BTN_EDIT | 1u << BTN_FX | 1u << BTN_ENV | 1u << BTN_SEL | 1u << BTN_LFO | 1u << BTN_GLO | 1u << BTN_ARP |
                         1u << BTN_SEQ)))
            return (int)changed;                /* (else a page button: on to that page, below) */
    } else if (buttons & (1u << BTN_HOME) && page != PG_PATCH) {
        ui_page(PG_SCOPE);
        return (int)changed;
    }
    if (page == PG_PATCH) {                     /* the PATCH screen: HOME, a page button or ALGORITHM leave it */
        if (buttons & (1u << BTN_HOME) || fm1_enc_take(ENC_ALGORITHM)) { ui_page(pa_back); return (int)changed; }
        if (!(buttons & (1u << BTN_EDIT | 1u << BTN_FX | 1u << BTN_ENV | 1u << BTN_SEL | 1u << BTN_LFO | 1u << BTN_GLO | 1u << BTN_ARP |
                         1u << BTN_SEQ)))
            return pa_input(buttons) | (int)changed;   /* (else a page button: on to that page, below) */
    } else if (buttons & (1u << BTN_SAVE)) {
        ui_page(PG_PATCH);
        return (int)changed;
    }
    if (buttons & (1u << BTN_EDIT)) ui_page(page == PG_OSC1 ? PG_OSC2 : PG_OSC1);
    if (buttons & (1u << BTN_FX))               /* FX: CHORUS, DELAY, REVERB in turn */
        ui_page(page == PG_CHO ? PG_DLY : page == PG_DLY ? PG_REV : PG_CHO);
    if (buttons & (1u << BTN_ENV)) ui_page(page == PG_AMP ? PG_FILTER : PG_AMP);
    if (buttons & (1u << BTN_SEL)) ui_page(PG_FILTER);   /* SELECT (the button): straight to FILTER */
    if (buttons & (1u << BTN_LFO)) ui_page(page == PG_LFO ? PG_LFO2 : PG_LFO);
    if (buttons & (1u << BTN_ARP)) {            /* ARP: its page; on its page, the arp on / off */
        if (page == PG_ARP) {
            edit.arp_on = !edit.arp_on;
            changed = 1;
            dirty |= D_HDR | D_GFX | D_ROWS;
        } else
            ui_page(PG_ARP);
    }
    if (buttons & (1u << BTN_SEQ)) ui_page(PG_SEQ);
    if (buttons & (1u << BTN_GLO)) ui_page(page == PG_MIX ? PG_SETUP : PG_MIX);
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0)
        ui_page((uint32_t)(((int32_t)page + d % (int32_t)PG_N + (int32_t)PG_N) % (int32_t)PG_N));
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) {
        int32_t n = (int32_t)row[page] + d, max = PAGES[page].n - 1;
        row[page] = (uint32_t)(n < 0 ? 0 : n > max ? max : n);
        pop_until = 0;
        dirty |= D_HDR | D_GFX | D_LEG | D_ROWS;
    }
    r = &PAGES[page].rows[row[page]];
    for (i = 0; i < 4u; i++) {
        const prm_t *p = &r->p[i];
        uint8_t *v;
        int32_t x;
        if ((d = fm1_enc_take(ENC_KNOB1 + i)) == 0 || p->kind == K_NONE || (p->kind == K_BPM && gset.clock))
            continue;                           /* (on the MIDI clock the tempo is the clock's) */
        if (p->kind == K_MODE || p->kind == K_LFOW || p->kind == K_ONOFF || p->kind == K_ARPM || p->kind == K_GLIDEM ||
            p->kind == K_CLK || p->kind == K_STRIG || p->kind == K_SSLIDE) {
            /* one step per two detents: no flicker */
            slow[i] += d;
            if (slow[i] > -2 && slow[i] < 2) continue;
            d = slow[i] / 2;
            slow[i] = 0;
        } else if (p->max >= 100u && fm1_ms - last_turn[i] < 60u) {
            d *= 3;                             /* a fast turn covers the range quickly */
        }
        last_turn[i] = fm1_ms;
        v = pval(page, p);
        x = (int32_t)*v + d;
        x = x < (p->kind == K_SLEN ? 1 : 0) ? (p->kind == K_SLEN ? 1 : 0) : x > p->max ? p->max : x;
        if ((p->kind == K_LDEST || p->kind == K_L2DEST) && (x == DEST_LFORATE || x == DEST_LFOAMT))
            /* the LFOs: past the mod envelope's own (LFORATE, LFOAMT) */
            x = d > 0 ? DEST_SPREAD : DEST_MIX;
        if (x == *v)
            continue;                           /* at the end of its range: nothing changed */
        *v = (uint8_t)x;
        if (p->kind != K_CURSOR) {              /* its name and value, large, for a moment */
            char val[16];
            fmt(val, p, v);
            pop(p->label, val);
        }
        if (p->kind == K_CURSOR) {              /* the cursor: not the sound */
            dirty |= D_GFX | D_ROWS;
            continue;
        }
        if (PAGES[page].osc == G) {             /* a setting, not the sound: saved; the clock to the engine */
            gset_dirty = 1;
            engine_sync(gset.clock);
            dirty |= D_GFX | D_ROWS;
            continue;
        }
        changed = 1;
        dirty |= D_HDR | D_GFX | D_ROWS;        /* (the header: the unsaved-changes mark) */
    }
    return (int)changed;
}

/* step entry: on the SEQ page with REC on, a key (or MIDI note) writes its note at the cursor (a rest becomes a note;
 * an accent stays one), and the cursor moves on (round within the length). 1: the sound changed */
static int ui_seq_key(uint32_t note)
{
    uint32_t len = edit.seq_len >= 1u && edit.seq_len <= 64u ? edit.seq_len : 16u;
    if (page != PG_SEQ || !seq_rec || note > 127u)
        return 0;
    edit.seq_note[seq_cursor] = (uint8_t)note;
    if (edit.seq_trig[seq_cursor] == SEQ_REST) edit.seq_trig[seq_cursor] = SEQ_NOTE;
    seq_cursor = (uint8_t)(seq_cursor + 1u >= len ? 0u : seq_cursor + 1u);
    dirty |= D_HDR | D_GFX | D_ROWS;
    return 1;
}

static void ui_init(int32_t slot)                /* the sound in that slot (the last one used), else the default */
{
    patches_load();
    edit = PATCH_INIT;
    if (slot >= 0 && slot < (int32_t)PATCH_SLOTS && patch_get((uint32_t)slot, &edit) == 0)
        cur_slot = slot;
    clean = edit;
    engine_set(&edit);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    page = PG_OSC1;
    dirty = D_ALL;
}

static void ui_draw(void)                       /* from the main loop: what changed, at most every UI_FRAME_MS */
{
    static uint32_t last, last_hdr;
    if (fm1_ms - last_hdr >= 500u) {            /* the CPU and the battery in the header, twice a second */
        last_hdr = fm1_ms;
        dirty |= D_HDR | (page == PG_SETUP ? D_GFX : 0u);   /* (SETUP: the battery's reading too) */
    }
    if (page == PG_SCOPE) {                     /* the scope: the trace every SC_FRAME_MS, the text when it changes */
        static uint32_t last_sc;
        if (fm1_ms - last_sc >= SC_FRAME_MS) { last_sc = fm1_ms; sc_draw(); }
        if (dirty & (D_ROWS | D_LEG) || sc_shown != (sc_note() << 8 | sc_cycles << 4 | sc_gain))
            sc_text();                          /* (the note, the cycles or the zoom changed) */
        dirty = 0;
        return;
    }
    if (page == PG_ARP && arp_drawn != arp.steps)
        dirty |= D_GFX;                         /* (the arp's picture follows the playing step) */
    if (page == PG_SEQ && seq_drawn != seq.steps)
        dirty |= D_GFX;                         /* (and the sequencer's) */
    {
        static uint32_t bpm_drawn;
        if (gset.clock && (page == PG_ARP || page == PG_SETUP) && bpm_drawn != mc_bpm10 && fm1_ms - last_hdr < 5u) {
            bpm_drawn = mc_bpm10;               /* (the clock's tempo, with the header's twice-a-second refresh) */
            dirty |= D_GFX | D_ROWS;
        }
    }
    if (pop_drawn && fm1_ms >= pop_until)
        dirty |= D_GFX;                         /* (the pop-up's time is up: the picture back) */
    if (!dirty || fm1_ms - last < UI_FRAME_MS)
        return;
    last = fm1_ms;
    if (page == PG_PATCH) {
        if (dirty & D_GFX) pa_draw_gfx();
        if (dirty & (D_ROWS | D_LEG)) pa_draw_text();   /* (not on the header's twice-a-second CPU refresh) */
        dirty = 0;
        return;
    }
    if (dirty & D_HDR) draw_hdr();
    if (dirty & D_GFX) draw_gfx();
    if (dirty & (D_LEG | D_ROWS)) draw_focus();
    if (dirty & D_LEG) draw_tabs();
    dirty = 0;
}
