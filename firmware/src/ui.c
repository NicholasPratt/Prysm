/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's pages (docs/PRYSM-DESIGN.md): OSC 1, OSC 2, FILTER, AMP, MIX.
 *
 *   ALGORITHM        the page (or EDIT = OSC 1 / OSC 2, FX = FILTER, ENV = AMP, GLO = MIX)
 *   SELECT           the row
 *   KNOB1 .. KNOB4   the row's parameters, left to right
 *
 * The screen, top to bottom: the header (the page and the row; the page number and the CPU), the page's graphic
 * (what the row being edited touches, lit), the legend (the row's four labels, over the knobs' cells), the rows (a
 * marker each, as in the sketch, then the four cells). Only what changed is redrawn, at most every UI_FRAME_MS.
 * Pages are tables (page_t -> row_t -> prm_t): a parameter is a field of patch_t, its range and how it shows. */

#define C_ACC RGB(180, 140, 255)                /* lilac: what is being edited */
#define C_MID RGB(165, 165, 175)
#define C_FAINT RGB(80, 80, 90)
#define C_BOX RGB(55, 55, 70)
#define C_SEL RGB(28, 22, 44)                   /* the selected row's band */

#define HDR_Y 0u
#define G_Y 20u                                 /* the graphic: 240 x G_H */
#define G_H 64u
#define LEG_Y 86u                               /* the legend */
#define ROW_Y 104u                              /* the rows: ROW_H each */
#define ROW_H 22u
#define CELL_X 14                               /* the four cells, after the markers */
#define CELL_W 56
#define UI_FRAME_MS 30u

/* ---- the parameters */
enum { K_NONE, K_NUM, K_BIP, K_CENT, K_BANK, K_POS, K_RATIO, K_MODE, K_SEMI };
typedef struct { const char *label; uint8_t off, max, kind; } prm_t;   /* off: in patch_t (osc pages: in osc[0]) */
typedef struct { const char *name; prm_t p[4]; uint16_t lit; } row_t;  /* lit: what the graphic lights (LIT_...) */
typedef struct { const char *name; const row_t *rows; uint8_t n, osc; } page_t;   /* osc: 1, 2, or 0 = none */

enum { LIT_C = 1, LIT_M1 = 2, LIT_M2 = 4, LIT_A1 = 8, LIT_A2 = 16, LIT_CURVE = 32, LIT_ENV = 64, LIT_ALL = 127,
       LIT_AR = 128 };                          /* (OSC pages: the mods' AR shapes under their boxes, not the names) */
#define OO(f) ((uint8_t)__builtin_offsetof(patch_t, osc[0].f))
#define PO(f) ((uint8_t)__builtin_offsetof(patch_t, f))
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
    {"DRIVE", {{"DRIVE", PO(drive), 127, K_NUM}, NOPRM, NOPRM, NOPRM}, LIT_CURVE},
};
static const row_t AMP_ROWS[] = {
    {"ENV", {{"A", PO(amp_a), 127, K_NUM}, {"D", PO(amp_d), 127, K_NUM},
             {"S", PO(amp_s), 127, K_NUM}, {"R", PO(amp_r), 127, K_NUM}}, LIT_ENV},
};
static const row_t MIX_ROWS[] = {
    {"OSC", {{"OSC1", PO(lvl1), 127, K_NUM}, {"OSC2", PO(lvl2), 127, K_NUM},
             {"SEMI", PO(semi2), 48, K_SEMI}, {"FINE", PO(fine2), 127, K_CENT}}, LIT_ALL},
};
#define NROWS(a) ((uint8_t)(sizeof a / sizeof a[0]))
enum { PG_OSC1, PG_OSC2, PG_FILTER, PG_AMP, PG_MIX, PG_N };
static const page_t PAGES[PG_N] = {
    {"OSC 1", OSC_ROWS, NROWS(OSC_ROWS), 1}, {"OSC 2", OSC_ROWS, NROWS(OSC_ROWS), 2},
    {"FILTER", FILTER_ROWS, NROWS(FILTER_ROWS), 0}, {"AMP", AMP_ROWS, NROWS(AMP_ROWS), 0},
    {"MIX", MIX_ROWS, NROWS(MIX_ROWS), 0},
};

static patch_t edit;                            /* the sound being edited (engine_set() after each change) */
static uint32_t page, row[PG_N];
static uint32_t dirty;                          /* what to redraw: D_... */
enum { D_HDR = 1, D_GFX = 2, D_LEG = 4, D_ROWS = 8, D_ALL = 15 };

static uint8_t *pval(uint32_t pg, const prm_t *p)
{
    uint32_t base = PAGES[pg].osc ? (PAGES[pg].osc - 1u) * (uint32_t)sizeof edit.osc[0] : 0u;
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

static void cv_adsr(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t a, uint32_t d, uint32_t s, uint32_t r,
                    uint16_t c)                 /* an envelope's shape: times as widths, the sustain as a height */
{
    int32_t seg = w / 4, aw = 1 + (int32_t)a * (seg - 1) / 127, dw = 1 + (int32_t)d * (seg - 1) / 127;
    int32_t rw = 1 + (int32_t)r * (seg - 1) / 127, sy = y + h - (int32_t)s * h / 127;
    int32_t x1 = x + aw, x2 = x1 + dw, x4 = x + w - 1, x3 = x4 - rw;
    cv_line(x, y + h, x1, y, c);
    cv_line(x1, y, x2, sy, c);
    cv_line(x2, sy, x3, sy, c);
    cv_line(x3, sy, x4, y + h, c);
}

/* ---- the graphics, one per kind of page */
static const int32_t BOX_X[NOP] = {14, 98, 182}; /* carrier, mod 1, mod 2: right to left is the signal's way */
#define BOX_W 52
#define BOX_H 42

static void gfx_osc(uint32_t o, uint32_t lit)   /* the sketch: carrier <- mod 1 <- mod 2 */
{
    uint32_t k, i;
    for (k = 0; k < NOP; k++) {                 /* the boxes: each operator's wave as the engine plays it */
        static const uint8_t LITS[NOP] = {LIT_C, LIT_M1, LIT_M2};
        const int16_t *t = op_cur[o][k];
        uint16_t edge = lit & LITS[k] ? C_ACC : C_BOX, line = lit & LITS[k] ? C_WHITE : C_MID;
        int32_t x0 = BOX_X[k], px = 0, py = 0;
        char s[16];
        cv_rect(x0, 0, BOX_W, 1, edge); cv_rect(x0, BOX_H - 1, BOX_W, 1, edge);
        cv_rect(x0, 0, 1, BOX_H, edge); cv_rect(x0 + BOX_W - 1, 0, 1, BOX_H, edge);
        for (i = 0; t && i < (uint32_t)BOX_W - 6u; i++) {
            int32_t x = x0 + 3 + (int32_t)i, y = BOX_H / 2 - t[i * PW_N / ((uint32_t)BOX_W - 6u)] * (BOX_H / 2 - 4) / 32767;
            if (i) cv_line(px, py, x, y, line);
            px = x; py = y;
        }
        if (lit & LIT_AR && k) {                /* the ENV row: the modulator's AR (attack up, hold, release down) */
            int32_t aw = 1 + (int32_t)edit.osc[o].att[k - 1u] * 15 / 127, rw = 1 + (int32_t)edit.osc[o].rel[k - 1u] * 15 / 127;
            int32_t xa = x0 + 6, xh = xa + aw, xr = x0 + BOX_W - 6 - rw, yb = G_H - 3, yt = BOX_H + 4;
            cv_line(xa, yb, xh, yt, C_ACC);
            cv_line(xh, yt, xr, yt, C_ACC);
            cv_line(xr, yt, xr + rw, yb, C_ACC);
            continue;
        }
        put(s, PW_WAVE_NAME[edit.osc[o].bank[k] < PW_BANKS ? edit.osc[o].bank[k] : 0u][wave_index(edit.osc[o].pos[k])]);
        cv_text(x0 + (BOX_W - text_w(&FONT_S, s)) / 2, BOX_H + 3, &FONT_S, s, lit & LITS[k] ? C_WHITE : C_MID);
    }
    for (k = 0; k < 2u; k++) {                  /* the arrows: mod k+1 into the one on its left; thicker = more */
        int32_t xr = BOX_X[k + 1u] - 2, xl = BOX_X[k] + BOX_W + 1, y = BOX_H / 2, mx = (xl + xr) / 2;
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

static void gfx_filter(uint32_t lit)
{
    /* the response: |H|^2 = 1 / ((1 - x^2)^2 + k^2 x^2), x = f / fc, over MIDI 16 .. 135, +24 .. -36 dB */
    const int32_t W = 160, top = 2, h = G_H - 4;
    uint64_t k12 = eg.f_k >> 12;
    int32_t px, py = 0, cx = (eg.f_cut - 64) * (W - 1) / 476;
    uint16_t c = lit & LIT_CURVE ? C_ACC : C_MID;
    for (px = 0; px < W; px += 8)               /* the 0 dB line, dotted; the cutoff */
        cv_pset(px, top + 24, C_FAINT);
    for (py = top; py < top + h; py += 3)
        cv_pset(cx, py, C_FAINT);
    for (px = 0; px < W; px++) {
        int32_t dq = 64 + px * 476 / (W - 1) - eg.f_cut, e = dq >= 0 ? dq / 24 : -((-dq + 23) / 24), f = dq - 24 * e;
        uint64_t x2 = PR_EXP2_24_Q12[f], den, one = 4096;
        int32_t l2, db, y;
        if (e >= 12) x2 = 1u << 24;
        else if (e >= 0) x2 <<= e;
        else x2 = -e >= 31 ? 0 : x2 >> -e;
        den = (x2 > one ? (x2 - one) * (x2 - one) : (one - x2) * (one - x2)) + ((k12 * k12 * x2) >> 12);
        if (!den) den = 1;
        l2 = msb64(den) * 256;                  /* log2(den) in Q8 (a straight line between the powers of 2) */
        l2 += (int32_t)((l2 >= 8 * 256 ? den >> (l2 / 256 - 8) : den << (8 - l2 / 256)) & 255u);
        db = (-(l2 - 24 * 256) * 3083) / (1024 * 256);   /* -10 log10(den / 2^24) = -3.0103 log2(...) */
        y = top + 24 - db;
        if (y < top) y = top;
        if (y > top + h - 1) y = top + h - 1;
        if (px) cv_line(px - 1, py, px, y, c);
        py = y;
    }
    if (edit.drive) {                           /* drive: a little bar under the curve */
        cv_rect(0, G_H - 2, (int32_t)edit.drive * W / 127, 2, lit & LIT_CURVE ? C_ACC : C_MID);
    }
    for (py = 4; py < (int32_t)G_H - 4; py += 2)   /* a faint divider: the response | the filter's envelope */
        cv_pset(166, py, C_FAINT);
    cv_adsr(172, 6, 64, G_H - 12, edit.flt_a, edit.flt_d, edit.flt_s, edit.flt_r, lit & LIT_ENV ? C_ACC : C_MID);
}

static void gfx_mix(void)
{
    char s[24], *e;
    cv_text(4, 4, &FONT_S, "OSC1", C_MID);
    cv_rect(48, 8, (int32_t)edit.lvl1 * 186 / 127, 8, C_ACC);
    cv_text(4, 28, &FONT_S, "OSC2", C_MID);
    cv_rect(48, 32, (int32_t)edit.lvl2 * 186 / 127, 8, C_ACC);
    e = put(s, "OSC2 ");
    e = put(snum(e, (int32_t)edit.semi2 - 24), "st ");
    put(snum(e, ((int32_t)edit.fine2 - 64) * 50 / 63), "c");
    cv_text(48, 46, &FONT_S, s, C_MID);
}

static void draw_gfx(void)
{
    uint32_t lit = PAGES[page].rows[row[page]].lit;
    cv_begin(240, G_H, C_BLACK);
    switch (page) {
    case PG_OSC1: gfx_osc(0, lit); break;
    case PG_OSC2: gfx_osc(1, lit); break;
    case PG_FILTER: gfx_filter(lit); break;
    case PG_AMP: cv_adsr(8, 4, 224, G_H - 10, edit.amp_a, edit.amp_d, edit.amp_s, edit.amp_r, C_ACC); break;
    default: gfx_mix();
    }
    cv_blit(0, G_Y);
}

/* ---- the header, the legend, the rows */
static void draw_hdr(void)
{
    char s[32], *e;
    e = put(s, PAGES[page].name);
    e = put(e, "  ");
    put(e, PAGES[page].rows[row[page]].name);
    draw_text_box(4, HDR_Y, 150, &FONT_S, s, C_ACC, 0);
    e = num(s, page + 1u);
    e = put(e, "/");
    e = num(e, PG_N);
#ifndef DINGHY_HOST
    e = put(e, "  ");
    e = num(e, cpu_max * 100u / RENDER_TICKS);  /* the longest render since the last header: the peak */
    put(e, "%");
    cpu_max = 0;
#endif
    draw_text_box(154, HDR_Y, 82, &FONT_S, s, C_FAINT, 2);
}

static void draw_legend(void)
{
    const row_t *r = &PAGES[page].rows[row[page]];
    uint32_t i;
    cv_begin(240, 16, C_BLACK);
    for (i = 0; i < 4u; i++)
        if (r->p[i].kind != K_NONE)
            cv_text(CELL_X + (int32_t)i * CELL_W + (CELL_W - text_w(&FONT_S, r->p[i].label)) / 2, 0, &FONT_S,
                    r->p[i].label, C_ACC);
    cv_blit(0, LEG_Y);
}

static void draw_rows(void)
{
    const page_t *pg = &PAGES[page];
    uint32_t n, i;
    cv_begin(240, (uint32_t)pg->n * ROW_H, C_BLACK);
    for (n = 0; n < pg->n; n++) {
        int32_t y = (int32_t)(n * ROW_H), sel = n == row[page];
        if (sel)
            cv_rect(0, y, 240, ROW_H - 2, C_SEL);
        cv_circle(6, y + 9, sel ? 4 : 3, sel ? C_ACC : C_FAINT, sel);   /* the sketch's markers */
        for (i = 0; i < 4u; i++) {
            const prm_t *p = &pg->rows[n].p[i];
            const uint8_t *v;
            char s[16];
            int32_t x = CELL_X + (int32_t)i * CELL_W;
            if (p->kind == K_NONE)
                continue;
            v = pval(page, p);
            fmt(s, p, v);
            cv_text(x + (CELL_W - text_w(&FONT_S, s)) / 2, y + 1, &FONT_S, s, sel ? C_WHITE : C_MID);
            if (p->kind == K_NUM || p->kind == K_POS)         /* a level: a bar from the left */
                cv_rect(x + 4, y + 17, (int32_t)*v * (CELL_W - 8) / p->max, 2, sel ? C_ACC : C_FAINT);
            else if (p->kind == K_BIP || p->kind == K_CENT) { /* bipolar: from the middle */
                int32_t m = x + CELL_W / 2, d = ((int32_t)*v - 64) * (CELL_W / 2 - 4) / 63;
                cv_rect(d < 0 ? m + d : m, y + 17, d < 0 ? -d : d + 1, 2, sel ? C_ACC : C_FAINT);
            }
        }
    }
    cv_blit(0, ROW_Y);
}

static void ui_page(uint32_t pg)
{
    if (pg >= PG_N) return;
    if (pg != page) lcd_fill(0, G_Y, 240, 240 - G_Y, C_BLACK);   /* (row counts differ: clear what was below) */
    page = pg;
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
    if (buttons & (1u << BTN_EDIT)) ui_page(page == PG_OSC1 ? PG_OSC2 : PG_OSC1);
    if (buttons & (1u << BTN_FX)) ui_page(PG_FILTER);
    if (buttons & (1u << BTN_ENV)) ui_page(PG_AMP);
    if (buttons & (1u << BTN_GLO)) ui_page(PG_MIX);
    if ((d = fm1_enc_take(ENC_ALGORITHM)) != 0)
        ui_page((uint32_t)(((int32_t)page + d % (int32_t)PG_N + (int32_t)PG_N) % (int32_t)PG_N));
    if ((d = fm1_enc_take(ENC_SELECT)) != 0) {
        int32_t n = (int32_t)row[page] + d, max = PAGES[page].n - 1;
        row[page] = (uint32_t)(n < 0 ? 0 : n > max ? max : n);
        dirty |= D_HDR | D_GFX | D_LEG | D_ROWS;
    }
    (void)fm1_enc_take(ENC_PRESETS);            /* (not used yet) */
    r = &PAGES[page].rows[row[page]];
    for (i = 0; i < 4u; i++) {
        const prm_t *p = &r->p[i];
        uint8_t *v;
        int32_t x;
        if ((d = fm1_enc_take(ENC_KNOB1 + i)) == 0 || p->kind == K_NONE)
            continue;
        if (p->kind == K_MODE) {                /* a mode: one step per two detents, so it does not flicker */
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
        *v = (uint8_t)(x < 0 ? 0 : x > p->max ? p->max : x);
        changed = 1;
        dirty |= D_GFX | D_ROWS;
    }
    return (int)changed;
}

static void ui_init(void)
{
    edit = PATCH_INIT;
    engine_set(&edit);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    page = PG_OSC1;
    dirty = D_ALL;
}

static void ui_draw(void)                       /* from the main loop: what changed, at most every UI_FRAME_MS */
{
    static uint32_t last, last_hdr;
    if (fm1_ms - last_hdr >= 500u) {            /* the CPU in the header, twice a second */
        last_hdr = fm1_ms;
        dirty |= D_HDR;
    }
    if (!dirty || fm1_ms - last < UI_FRAME_MS)
        return;
    last = fm1_ms;
    if (dirty & D_HDR) draw_hdr();
    if (dirty & D_GFX) draw_gfx();
    if (dirty & D_LEG) draw_legend();
    if (dirty & D_ROWS) draw_rows();
    dirty = 0;
}
