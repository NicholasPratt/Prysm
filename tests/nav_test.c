/* SPDX-License-Identifier: GPL-3.0-only */
/* The UI's navigation and what the focus row says, through the app's own code (ui.c): HOME (LIVE first, then the
 * SCOPE, then back; SETUP's HOME row the other way round), the SELECT button (FILTER from every page; on FILTER, two
 * parts: A and B in turn), the controls that do nothing as the sound is now (dimmed, and why), the envelopes' times
 * (as the engine's tables have them), and a knob turned lighting its own cell (not a pop-up over the picture).
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { printf("  %s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)

static void press(uint32_t b) { fm1_ms += 500; ui_input(1u << b); }

static const char *shown(const prm_t *p, const uint8_t *v)
{
    static char s[16];
    fmt(s, p, v);
    return s;
}

int main(void)
{
    static const prm_t ET = {"A", 0, 127, K_ETIME};
    uint8_t v;
    printf("navigation checks\n");
    fm1_ms = 10000;
    engine_init();
    ui_init(-1, -1);
    engine_set(&edit);

    /* HOME: LIVE, the SCOPE, back */
    ui_page(PG_AMP);
    press(BTN_HOME);
    CHECK(page == PG_LIVE, "HOME on AMP: LIVE");
    press(BTN_HOME);
    CHECK(page == PG_SCOPE, "HOME again: the SCOPE");
    press(BTN_HOME);
    CHECK(page == PG_AMP, "HOME there: back to AMP");
    gset.home = 1;
    press(BTN_HOME);
    CHECK(page == PG_SCOPE, "SETUP's HOME on SCOPE: the SCOPE first");
    press(BTN_HOME);
    CHECK(page == PG_LIVE, "... then LIVE");
    press(BTN_HOME);
    CHECK(page == PG_AMP, "... then back to AMP");
    gset.home = 0;
    ui_page(PG_MIX);
    press(BTN_HOME);
    press(BTN_SAVE);
    CHECK(page == PG_PATCH, "SAVE on LIVE: the PATCH screen");
    press(BTN_HOME);
    CHECK(page == PG_LIVE, "HOME there: back to LIVE");
    press(BTN_HOME);
    press(BTN_HOME);
    CHECK(page == PG_MIX, "HOME, HOME: the SCOPE, then MIX (where HOME was first pressed)");

    /* the SELECT button: FILTER from anywhere; on FILTER, the parts in turn */
    edit.vmode = VM_SINGLE;
    ui_part = 0;
    ui_page(PG_OSC1);
    press(BTN_SEL);
    CHECK(page == PG_FILTER, "one part, SELECT on OSC 1: FILTER");
    press(BTN_SEL);
    CHECK(page == PG_FILTER && ui_part == 0, "one part, SELECT on FILTER: nothing (no part B)");
    edit.vmode = VM_DUAL;
    ui_page(PG_CHO);
    press(BTN_SEL);
    CHECK(page == PG_FILTER && ui_part == 0, "two parts, SELECT on CHORUS: FILTER, still part A");
    press(BTN_SEL);
    CHECK(page == PG_FILTER && ui_part == 1, "SELECT on FILTER: part B");
    fm1_ms += 100;                              /* (quickly: not a double press any more) */
    ui_input(1u << BTN_SEL);
    CHECK(page == PG_FILTER && ui_part == 0, "SELECT again, quickly: part A (FILTER stays)");
    press(BTN_SEL);
    ui_page(PG_AMP);
    press(BTN_SEL);
    CHECK(page == PG_FILTER && ui_part == 1, "part B, SELECT on AMP: FILTER, part B kept");
    ui_page(PG_AMP);
    ui_view();
    CHECK(hdr_part() == 1, "the header on AMP: part B");
    ui_page(PG_CHO);
    CHECK(hdr_part() == 2, "the header on CHORUS: ALL (shared)");
    ui_page(PG_OSC2);
    CHECK(hdr_part() == 1, "the header on OSC 2: part B's oscillator");
    ui_page(PG_SEQ);
    CHECK(hdr_part() == 1, "the header on SEQ: part B (where the keys record)");
    ui_page(PG_SETUP);
    CHECK(hdr_part() == 3, "the header on SETUP: no part (not the sound)");
    ui_part = 0;
    edit.vmode = VM_SINGLE;
    ui_view();

    /* what does nothing now: dimmed, and why */
    edit.flt_type = FLT_LP12;
    CHECK(prm_idle(PG_FILTER, &FILTER_ROWS[2].p[0]) != 0, "LP12: COMB's DAMP does nothing (%s)",
          prm_idle(PG_FILTER, &FILTER_ROWS[2].p[0]));
    edit.flt_type = FLT_COMBP;
    CHECK(prm_idle(PG_FILTER, &FILTER_ROWS[2].p[0]) == 0, "COMB+: it does");
    edit.hp_cut = 0;
    CHECK(prm_idle(PG_FILTER, &FILTER_ROWS[1].p[3]) != 0, "HPF off: HP RES does nothing");
    edit.lfo2_sync = 0;
    CHECK(prm_idle(PG_LFO2, &LFO2_ROWS[0].p[1]) == 0, "LFO 2 free: its RATE works");
    edit.lfo2_sync = 3;
    CHECK(prm_idle(PG_LFO2, &LFO2_ROWS[0].p[1]) != 0 && prm_idle(PG_LFO2, &LFO2_ROWS[0].p[2]) == 0,
          "LFO 2 synced: RATE does nothing, SYNC does");
    edit.mfx_type = 0;
    CHECK(prm_idle(PG_MFX, &MFX_ROWS[0].p[2]) != 0 && prm_idle(PG_MFX, &MFX_ROWS[0].p[0]) == 0,
          "MOD FX off: MIX does nothing, TYPE does");
    edit = PATCH_INIT;

    /* the rows' knobs: MIX third on every effect; DEST, AMT first on the mod envelopes (as on the LFOs) */
    CHECK(MFX_ROWS[0].p[2].off == PO(mfx_mix) && CHO_ROWS[0].p[2].off == PO(cho_mix) &&
          DLY_ROWS[0].p[2].off == PO(dly_mix) && REV_ROWS[0].p[2].off == PO(rev_mix), "MIX: knob 3 on every effect");
    CHECK(MENV_ROWS[1].p[0].off == PO(menv_dest) && MENV_ROWS[1].p[1].off == PO(menv_amt) &&
          MENV2_ROWS[1].p[0].off == PO(menv2_dest) && MENV2_ROWS[1].p[1].off == PO(menv2_amt) &&
          LFO2_ROWS[1].p[0].off == PO(lfo2_dest) && LFO2_ROWS[1].p[1].off == PO(lfo2_amt),
          "DEST, AMT: knobs 1, 2 on M.ENV 1, M.ENV 2, LFO 2");

    /* the envelopes' times: 1 ms .. 10 s, exponential (gen_prysm_tables.py env_time) */
    v = 0;
    CHECK(name_eq(shown(&ET, &v), "1.0ms"), "0: %s", shown(&ET, &v));
    v = 64;
    CHECK(name_eq(shown(&ET, &v), "104ms"), "64: %s", shown(&ET, &v));
    v = 96;
    CHECK(name_eq(shown(&ET, &v), "1.0s") || name_eq(shown(&ET, &v), "1.1s"), "96: %s", shown(&ET, &v));
    v = 127;
    CHECK(name_eq(shown(&ET, &v), "10.0s"), "127: %s", shown(&ET, &v));
    CHECK(AMP_ROWS[0].p[0].kind == K_ETIME && AMP_ROWS[0].p[2].kind == K_NUM && AMP_ROWS[0].p[3].kind == K_ETIME,
          "AMP: A, D, R times; S a level");

    /* a knob turned: its cell lit (no pop-up over the picture) */
    ui_page(PG_AMP);
    fm1_ms += 3000;
    host_enc[ENC_KNOB1 + 1u] = 1;
    ui_input(0);
    CHECK(cell_k == 1 && pop_until == 0, "KNOB2 on AMP: its cell lit, no pop-up");
    fm1_ms += POP_MS + 10u;
    draw_focus();
    CHECK(cell_k == -1, "after POP_MS: not lit");
    ui_page(PG_FILTER);
    host_enc[ENC_PRESETS] = 1;
    ui_input(0);
    CHECK(cell_k == 0 && pop_until == 0, "PRESETS on FILTER: CUT's cell lit");
    ui_page(PG_AMP);
    host_enc[ENC_PRESETS] = 1;
    ui_input(0);
    CHECK(cell_k == -1 && pop_until > fm1_ms, "PRESETS on AMP: the cutoff's pop-up (no cell shows it)");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
