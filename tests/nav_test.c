/* SPDX-License-Identifier: GPL-3.0-only */
/* The UI's navigation and what the focus row says, through the app's own code (ui.c): HOME (LIVE first, then the
 * SCOPE, then ROUTE, then back; SETUP's HOME row LIVE and the SCOPE the other way round), the ROUTE screen's knobs, the SELECT button (FILTER from every page; on FILTER, two
 * parts: A and B in turn), the controls that do nothing as the sound is now (dimmed, and why), the envelopes' times
 * (as the engine's tables have them), a knob turned lighting its own cell (not a pop-up over the picture); the OSC
 * pages' rows by operator, the SEQ page's groups, an action that clears asked first, the PATCH screen's questions.
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

    /* HOME: LIVE, the SCOPE, ROUTE, back */
    ui_page(PG_AMP);
    press(BTN_HOME);
    CHECK(page == PG_LIVE, "HOME on AMP: LIVE");
    press(BTN_HOME);
    CHECK(page == PG_SCOPE, "HOME again: the SCOPE");
    press(BTN_HOME);
    CHECK(page == PG_ROUTE, "HOME there: ROUTE");
    press(BTN_HOME);
    CHECK(page == PG_AMP, "HOME there: back to AMP");
    gset.home = 1;
    press(BTN_HOME);
    CHECK(page == PG_SCOPE, "SETUP's HOME on SCOPE: the SCOPE first");
    press(BTN_HOME);
    CHECK(page == PG_LIVE, "... then LIVE");
    press(BTN_HOME);
    CHECK(page == PG_ROUTE, "... then ROUTE");
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
    press(BTN_HOME);
    CHECK(page == PG_MIX, "HOME x 3: the SCOPE, ROUTE, then MIX (where HOME was first pressed)");
    {                                           /* ALGORITHM: every page but ROUTE (HOME's) */
        uint32_t k, seen = 0;
        ui_page(PG_OSC1);
        for (k = 0; k < PG_N - 1u; k++) { host_enc[ENC_ALGORITHM] = 1; ui_input(0); seen |= page == PG_ROUTE; }
        CHECK(!seen && page == PG_OSC1, "ALGORITHM round: ROUTE not in it, every other page (back to OSC 1)");
        host_enc[ENC_ALGORITHM] = -1; ui_input(0);
        CHECK(page == PG_SETUP, "... back from OSC 1: SETUP (ROUTE skipped)");
    }

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

    /* OSC: a row an operator; a modulator's ratio its pitch in MIX or RING (on its row and on RATIO), the carrier's not */
    {
        edit = PATCH_INIT;
        edit.osc[0].mode[0] = MODE_FM;
        CHECK(prm_of(PG_OSC1, OR_MOD1, 2)->kind == K_RATIO && prm_of(PG_OSC1, OR_MOD1, 3)->off == OO(amt[0]),
              "MOD1: RATIO, AMT on knobs 3, 4");
        edit.osc[0].mode[0] = (uint8_t)(MODE_FM + 1u);
        CHECK(prm_of(PG_OSC1, OR_MOD1, 2) == &PITCH_PRM[0] && prm_of(PG_OSC1, OR_RATIO, 1) == &PITCH_PRM[0],
              "MOD1 not in FM: its ratio a pitch, on MOD1 and on RATIO");
        CHECK(prm_of(PG_OSC1, OR_MOD2, 2)->kind == K_RATIO && prm_of(PG_OSC1, OR_CAR, 2)->kind == K_RATIO,
              "MOD2 (in FM) and the carrier: ratios");
        CHECK(OSC_ROWS[OR_M1ENV].p[2].off == OO(mode[0]) && OSC_ROWS[OR_M2ENV].p[2].off == OO(mode[1]) &&
              OSC_ROWS[OR_M2ENV].p[0].off == OO(att[1]), "M1 ENV, M2 ENV: each modulator's A, R, MODE, FINE");
        edit = PATCH_INIT;
    }

    /* SEQ: its rows in groups */
    CHECK(seq_group(SR_STEP) == 0 && seq_group(SR_LOCK) == 0 && seq_group(SR_PATTERN) == 1 && seq_group(SR_PARTS) == 1 &&
          seq_group(SR_CLICK) == 2 && seq_group(SR_SONG) == 3 && seq_group(SR_ENTRY) == 3,
          "SEQ's groups: STEPS, PATTERN, REC, SONG");

    /* an action that clears: asked, SAVE does it, HOME not */
    {
        pattern_t *pt;
        ui_page(PG_SEQ);
        row[PG_SEQ] = SR_EDIT;
        seq_row = 0;
        pt = seq_ed();
        pt->trig[0] = SEQ_NOTE;
        fm1_ms += 1000;
        host_enc[ENC_KNOB1 + 3u] = 2;           /* CLEAR (two detents: one step) */
        ui_input(0);
        CHECK(act_kind == K_SCLEAR && pt->trig[0] == SEQ_NOTE, "CLEAR turned: asked (%s), nothing cleared yet", act_what);
        press(BTN_HOME);
        CHECK(!act_kind && pt->trig[0] == SEQ_NOTE && page == PG_SEQ, "HOME: cancelled, still on SEQ, the note kept");
        host_enc[ENC_KNOB1 + 3u] = 2;
        ui_input(0);
        press(BTN_SAVE);
        CHECK(!act_kind && pt->trig[0] == SEQ_REST && page == PG_SEQ, "again, SAVE: cleared (not the PATCH screen)");
        pt->trig[0] = SEQ_NOTE;
        host_enc[ENC_KNOB1 + 3u] = 2;
        ui_input(0);
        fm1_ms += ACT_MS + 100u;
        ui_input(1u << BTN_SAVE);
        CHECK(!act_kind && pt->trig[0] == SEQ_NOTE && page == PG_PATCH, "unanswered past ACT_MS: forgotten, SAVE as usual");
        ui_page(PG_SEQ);
        host_enc[ENC_KNOB1 + 1u] = 2;           /* COPY: at once */
        ui_input(0);
        CHECK(!act_kind && clip_rows == 1u, "COPY: at once, not asked");
    }

    /* the PATCH screen says what SAVE will do */
    ui_page(PG_PATCH);
    pa_row = PA_SAVE;
    pa_slot = 0;
    pa_do();
    edit.cut = (uint8_t)(edit.cut + 1u);
    pa_row = PA_LOAD;
    pa_armed = 0;
    pa_do();
    CHECK(!strcmp(pa_msg, "DISCARD EDITS? SAVE: LOAD 1"), "LOAD over edits: %s", pa_msg);
    pa_row = PA_SAVE;                           /* (slot 2 used; then the sound, from slot 1, saved over it) */
    pa_slot = 1;
    pa_armed = 0;
    pa_do();
    cur_slot = 0;
    pa_armed = 0;
    pa_do();
    CHECK(!strcmp(pa_msg, "OVERWRITE SLOT 2? SAVE"), "SAVE over another used slot: %s", pa_msg);

    {   /* every pin the grid has is a parameter too (the editor's, the plugin's, LIVE's knobs): ROUTE_ROWS has each */
        uint32_t r, c, i, k, all = 1, miss_r = 0, miss_c = 0;
        for (r = 0; r < RT_ROWS; r++)
            for (c = 0; c < RT_COLS; c++) {
                uint32_t off = (uint32_t)__builtin_offsetof(patch_t, rt[r][c]), found = 0;
                if (!rt_ok(r, c)) continue;
                for (i = 0; i < NROWS(ROUTE_ROWS); i++)
                    for (k = 0; k < 4u; k++)
                        found |= ROUTE_ROWS[i].p[k].kind == K_PIN && ROUTE_ROWS[i].p[k].off == off;
                if (!found && all) { all = 0; miss_r = r; miss_c = c; }
            }
        CHECK(all, "ROUTE: every pin the grid has is a parameter, U2 and U3 too%s%s%s", all ? "" : " (missing: ",
              all ? "" : RT_ROW_NAME[miss_r], all ? "" : RT_COL_SHORT[miss_c]);
    }
    /* the ROUTE screen: KNOB1 the row, KNOB2 the column (an effect's: those after it alone), KNOB3 the level, KNOB4
     * on / off; a drum's pin, an older sound's: its rows taken from the sequencer's sends first; one part: no FILT B */
    {
        edit = PATCH_INIT;
        edit.vmode = VM_SINGLE;
        ui_page(PG_ROUTE);
        rt_r = 0; rt_c = RC_FLT;
        host_enc[ENC_KNOB1 + 1u] = 12; ui_input(0);
        CHECK(rt_r == RT_OSC1 && rt_c == RC_U3, "KNOB2: the columns, past OUT to USB's U2, U3 (and no further)");
        host_enc[ENC_KNOB1 + 1u] = -2; ui_input(0);
        CHECK(rt_c == RC_OUT, "... back two: OUT (%u)", rt_c);
        host_enc[ENC_KNOB1 + 2u] = 50; ui_input(0);
        CHECK(edit.rt[RT_OSC1][RC_OUT] == 50, "KNOB3: OSC 1 > OUT at 50 (%u)", edit.rt[RT_OSC1][RC_OUT]);
        fm1_ms += 1000;
        host_enc[ENC_KNOB1 + 3u] = -2; ui_input(0);
        CHECK(edit.rt[RT_OSC1][RC_OUT] == 0, "KNOB4 back two detents: off");
        host_enc[ENC_KNOB1 + 3u] = 2; ui_input(0);
        CHECK(edit.rt[RT_OSC1][RC_OUT] == 127, "... on: 127");
        host_enc[ENC_KNOB1] = 8; ui_input(0);
        CHECK(rt_r == RT_CRUSH, "one part: KNOB1 past FILTER to CRUSH, no FILT B (%u)", rt_r);
        rt_c = RC_FLT; rt_move_row(0);
        CHECK(rt_c == RC_MFX, "CRUSH's row: its first column MOD FX (%u)", rt_c);
        host_enc[ENC_KNOB1 + 1u] = -3; ui_input(0);
        CHECK(rt_c == RC_MFX, "... KNOB2 left: no further (%u)", rt_c);
        edit.vmode = VM_DUAL;
        host_enc[ENC_KNOB1] = -1; ui_input(0);
        CHECK(rt_r == RT_FILTB, "two parts: FILT B above CRUSH");
        edit.rt[RT_KICK][RC_FLT] = RT_OLD;      /* (an older sound: the drums' from the sequencer) */
        seqs.dr_flt[1] = 0; seqs.dr_rev = 64;
        rt_r = RT_SNARE; rt_c = RC_REV;
        fm1_ms += 1000;                         /* (a slow turn: one step) */
        CHECK(rt_pin(&edit, RT_SNARE, RC_REV) == 64 && rt_pin(&edit, RT_SNARE, RC_OUT) == 127 &&
              rt_pin(&edit, RT_SNARE, RC_FLT) == 0, "an older sound: the snare's pins its sends (REV 64, OUT 127)");
        host_enc[ENC_KNOB1 + 2u] = 1; ui_input(0);
        CHECK(edit.rt[RT_KICK][RC_FLT] != RT_OLD && edit.rt[RT_SNARE][RC_REV] == 65 && edit.rt[RT_KICK][RC_OUT] == 127,
              "... a pin turned: the drums' rows the sound's own, from there (65, the kick out)");
        {
            char t[24];
            edit = PATCH_INIT;
            rt_where(t, RT_OSC1);
            CHECK(!strcmp(t, "FLT"), "OSC 1 as new: into the filter (%s)", t);
            edit.rt[RT_OSC1][RC_DLY] = 10; edit.rt[RT_OSC1][RC_REV] = 10; edit.rt[RT_OSC1][RC_OUT] = 10;
            rt_where(t, RT_OSC1);
            CHECK(!strcmp(t, "FLT DLY +2"), "... and more: %s", t);
        }
        edit = PATCH_INIT;
        ui_page(PG_AMP);
    }
    {                                           /* FX: the effects' pages in the ROUTE screen's order, round */
        static const uint8_t ORDER[7] = {PG_CRUSH, PG_MFX, PG_CHO, PG_DLY, PG_REV, PG_COMP, PG_CRUSH};
        uint32_t k, ok;
        ui_page(PG_AMP);
        for (k = 0, ok = 1; k < 7u; k++) { press(BTN_FX); ok &= page == ORDER[k]; }
        CHECK(ok, "FX: CRUSH, MOD FX, CHORUS, DELAY, REVERB, COMP, then CRUSH again");
        {
            char t4[4][16];
            strcpy(t4[0], shown(&COMP_ROWS[0].p[0], &edit.cmp_thr)); strcpy(t4[1], shown(&COMP_ROWS[0].p[1], &edit.cmp_ratio));
            strcpy(t4[2], shown(&COMP_ROWS[1].p[0], &edit.cmp_att)); strcpy(t4[3], shown(&COMP_ROWS[1].p[1], &edit.cmp_rel));
            CHECK(!strcmp(t4[0], "-18dB") && !strcmp(t4[1], "4:1") && !strcmp(t4[2], "3.2ms") && !strcmp(t4[3], "144ms"),
                  "COMP as new: %s %s %s %s", t4[0], t4[1], t4[2], t4[3]);
        }
        v = 0; CHECK(!strcmp(shown(&COMP_ROWS[1].p[0], &v), "0.1ms"), "... ATTACK at 0: 0.1 ms (%s)", shown(&COMP_ROWS[1].p[0], &v));
        CHECK(!strcmp(shown(&COMP_ROWS[1].p[1], &v), "10ms"), "... RELEASE at 0: 10 ms (%s)", shown(&COMP_ROWS[1].p[1], &v));
        v = 127; CHECK(!strcmp(shown(&COMP_ROWS[1].p[0], &v), "100ms"), "... ATTACK at 127: 100 ms");
        CHECK(!strcmp(shown(&COMP_ROWS[1].p[1], &v), "2.0s"), "... RELEASE at 127: 2.0 s (%s)", shown(&COMP_ROWS[1].p[1], &v));
        CHECK(!strcmp(shown(&COMP_ROWS[0].p[0], &v), "0dB"), "... THRESH at 127: 0 dB (%s)", shown(&COMP_ROWS[0].p[0], &v));
        ui_page(PG_AMP);
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
