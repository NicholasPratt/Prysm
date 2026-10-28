/* SPDX-License-Identifier: GPL-3.0-only */
/* The LIVE screen on a computer, through the app's own code (ui.c) and the engine: a pattern key at once while
 * stopped and on the next bar line while playing (from its top; a song drops to PATTERN mode), a patch key (its
 * sequencer on the bar line, its sound with it), pressing again to cancel, a stop bringing what waits at once, and the
 * knobs: a parameter chosen with PRESETS, turned with its knob.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { printf("  %s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)

static void run_frames(uint32_t n)              /* the engine, n frames on (renders of 256, as the ISR) */
{
    static int32_t blk[2u * 256u];
    for (; n >= 256u; n -= 256u)
        engine_render(blk, 256u);
}
#define STEP_F 5512u                            /* a 1/16 at 120 BPM: 5512.5 frames; a bar: 16 steps */
static uint32_t played;                         /* frames rendered since PLAY */
static void run_steps(uint32_t from, uint32_t to)   /* on to the middle of step `to` (counted from PLAY, 0 = the first) */
{
    static int32_t blk[2u * 256u];
    (void)from;
    while (played < to * 11025u / 2u + 2756u) { engine_render(blk, 256u); played += 256u; }
}

int main(void)
{
    patch_t a = PATCH_INIT, b = PATCH_INIT;
    uint32_t pg, r, k;
    int32_t n;
    printf("LIVE checks\n");
    a.arp_bpm = b.arp_bpm = 80;                 /* 120 BPM */
    a.cut = 10;
    b.cut = 100;
    seqs_init();                                /* slot 1: first note 60; slot 2: 72 */
    seqs.pat[0].note[0] = 60;
    seqstore_save(0, &seqs, sizeof seqs);
    seqs.pat[0].note[0] = 72;
    seqstore_save(1, &seqs, sizeof seqs);
    bank.version = PATCH_VERSION;
    bank.p[0] = a; bank.used[0] = 1;
    bank.p[1] = b; bank.used[1] = 1;
    store_write();
    memset(&bank, 0, sizeof bank);
    fm1_ms = 10000;
    engine_init();
    ui_init(0, 0);
    engine_set(&edit);
    engine_sync(0);
    ui_page(PG_LIVE);
    CHECK(cur_slot == 0 && seqs.pat[0].note[0] == 60 && edit.cut == 10, "slot 1 plays (cutoff 10, first note 60)");

    live_key(3);                                /* the second black key: P2, stopped: at once */
    CHECK(seqs.cur == 1u && lv_qpat < 0, "stopped: P2 at once");

    engine_seq_play(1);
    played = 0;
    run_steps(0, 5);
    live_key(5);                                /* P3 */
    CHECK(lv_qpat == 2 && seqs.cur == 1u, "playing, step 6: P3 waits");
    run_steps(5, 15);
    CHECK(seqs.cur == 1u && seq.playing_pat == 1u && seq.playing == 15u, "step 16, the bar's last: still P2");
    run_steps(15, 16);
    CHECK(seqs.cur == 2u && seq.playing_pat == 2u && seq.playing == 0u, "the bar line: P3, from its first step");
    live_poll();
    CHECK(lv_qpat < 0 && seqs_dirty, "the main loop follows (the choice saved with the sequencer)");

    seqs.mode = 1;                              /* a song playing: a pattern key drops it to PATTERN mode */
    live_key(1);
    run_steps(16, 31);
    CHECK(seqs.mode == 1u, "SONG, P1 waiting: the bar's last step, still the song");
    run_steps(31, 32);
    live_poll();
    CHECK(seqs.mode == 0u && seqs.cur == 0u && seq.playing_pat == 0u, "the bar line: PATTERN mode, P1");

    live_key(2);                                /* the second white key: slot 2 */
    CHECK(lv_qslot == 1 && seq_swap == 1u && edit.cut == 10, "slot 2 waits; slot 1's sound and sequencer play on");
    run_steps(32, 47);
    CHECK(seqs.pat[0].note[0] == 60 && seq_swap == 1u, "the bar's last step: still slot 1's");
    run_steps(47, 48);
    CHECK(seqs.pat[0].note[0] == 72 && seq_swap == 2u && seq.playing == 0u, "the bar line: slot 2's sequencer, from its top");
    n = live_poll();
    CHECK(n == 1 && edit.cut == 100 && cur_slot == 1 && seq_owner == 1, "... and its sound (cutoff 100)");
    engine_set(&edit);
    seqs_poll();

    live_key(0);                                /* slot 1, then pressed again: it stays */
    live_key(0);
    CHECK(lv_qslot < 0 && seq_swap == 0u, "pressed again: cancelled");
    run_steps(48, 65);
    live_poll();
    CHECK(cur_slot == 1 && seqs.pat[0].note[0] == 72, "the bar line: nothing changes");

    live_key(0);                                /* waiting, then a stop: at once */
    engine_seq_play(0);
    run_frames(256u);
    n = live_poll();
    CHECK(n == 1 && cur_slot == 0 && edit.cut == 10 && seqs.pat[0].note[0] == 60, "a stop: slot 1 comes in at once");
    seqs_poll();

    /* the knobs: K1 is the cutoff; PRESETS one on: the next parameter; KNOB1 turns it */
    n = lv_walk(-1, -1, &pg, &r, &k);
    CHECK(n > 100 && lv_walk(-1, (int32_t)__builtin_offsetof(patch_t, arp_bpm), &pg, &r, &k) >= 0,
          "the list: every parameter of the sound, once each (%d)", (int)n);
    CHECK(knob_prm(0, &pg)->off == __builtin_offsetof(patch_t, cut) && pg == PG_FILTER, "K1: FILTER CUT");
    lv_row = LV_K1;
    host_enc[ENC_PRESETS] = 1;
    live_input(0);
    CHECK(gset.knob[0] == __builtin_offsetof(patch_t, res) && gset_dirty, "PRESETS on K1: FILTER RES (saved with the settings)");
    lv_row = LV_PLAY;
    fm1_ms += 1000;
    edit.res = 40;
    host_enc[ENC_KNOB1] = 5;
    ui_input(0);
    CHECK(edit.res == 45 && modified(), "KNOB1: the resonance up 5 (the sound changed: unsaved)");

    /* the mutes: G4 the kick, D5 E5 the oscillators (on / off; not the settings) */
    CHECK(live_keys(14) && live_keys(19) && live_keys(24) && live_keys(26) && !live_keys(13) && !live_keys(10),
          "G4 .. G5's white keys taken; the black ones play");
    live_key(14);
    drum_hit(DR_KICK_S, 127, 0);
    CHECK(live_mute == 1u && !drv[0].d, "G4: the kick muted, its hits silent");
    live_key(14);
    drum_hit(DR_KICK_S, 127, 0);
    CHECK(live_mute == 0u && drv[0].d, "G4 again: back on");
    engine_all_off();
    run_frames(44100u * 3u);                    /* (the kick and every release rung out) */
    live_key(21);
    live_key(23);
    engine_note_on(60);
    {
        int64_t a = 0;
        uint32_t i, j;
        for (j = 0; j < 16u; j++) { static int32_t b[512]; engine_render(b, 256u); for (i = 0; i < 512u; i++) a += b[i] < 0 ? -b[i] : b[i]; }
        CHECK(a == 0, "D5, E5: both oscillators muted: silent");
        fm1_ms = 0;
        CHECK(!(live_leds() >> 21 & 1u) && (live_leds() >> 24 & 1u), "their LEDs flash (off now), NOISE's lit");
        fm1_ms = 120;
        CHECK((live_leds() >> 21 & 1u) && (live_leds() >> 24 & 1u), "... (on now)");
    }
    CHECK(live_leds() >> 26 & 1u, "G5 lit: something muted");
    live_key(14);
    live_key(26);
    CHECK(live_mute == 0u && !(live_leds() >> 26 & 1u), "G5: every mute off (the kick's too), its LED out");
    live_key(14);
    ui_page(PG_OSC1);
    CHECK(live_mute == 0u, "out of LIVE: the mutes all on");
    ui_page(PG_LIVE);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
