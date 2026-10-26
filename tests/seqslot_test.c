/* SPDX-License-Identifier: GPL-3.0-only */
/* Each slot's own sequencer on a computer, through the app's own code (patches.c, ui.c) and the engine: the upgrade
 * from one sequencer (each used slot gets a copy), LOAD bringing a slot's in, SOUND leaving the one playing, SAVE
 * writing both (and asking before it replaces a slot's sequencer), LOAD while playing (taken over where the next
 * pattern starts), a slot without one taking the one playing, the power-on, and the store's own kept as it was.
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
#define STEP_F 5512u                            /* a 1/16 at 120 BPM: 5512.5 frames */

static uint8_t first_note(void) { return seqs.pat[0].note[0]; }   /* (each test sequencer: its own first note) */
static uint8_t area_note(uint32_t s)            /* the first note of slot s's saved sequencer (0: none) */
{
    seqs_t q;
    return seqstore_load(s, &q, sizeof q) == 0 ? q.pat[0].note[0] : 0u;
}
static int press(uint32_t row, uint32_t slot)   /* the PATCH screen: row, slot, SAVE */
{
    pa_row = row;
    pa_slot = slot;
    return pa_do();
}

int main(void)
{
    seqs_t q;
    printf("slot sequencer checks\n");
    /* a store from 1.2: slots 1 and 3 used, the one sequencer (first note 60), no slot with its own */
    edit = PATCH_INIT;
    edit.arp_bpm = 80;                          /* 120 BPM */
    seqs_init();
    seqs.pat[0].note[0] = 60;
    bank.version = PATCH_VERSION;
    bank.p[0] = edit; bank.used[0] = 1;
    bank.p[2] = edit; bank.used[2] = 1;
    store_write();
    memset(&bank, 0, sizeof bank);
    memset(&seqs, 0, sizeof seqs);

    engine_init();
    ui_init(0, 0);                              /* power-on: slot 1's sound and sequencer */
    CHECK(area_note(0) == 60 && area_note(2) == 60 && !host_seqs_n[1], "upgrade: slots 1 and 3 get the sequencer, 2 none");
    CHECK(seq_owner == 0 && first_note() == 60, "slot 1's sequencer plays");

    seqs.pat[0].note[0] = 72;                   /* slot 1's pattern changed (saved by the app, when quiet) */
    seqs_write();
    press(PA_LOAD, 2);
    seqs_poll();
    CHECK(first_note() == 60 && seq_owner == 2 && cur_slot == 2, "LOAD 3: its sound and its sequencer (60)");
    press(PA_LOAD, 0);
    seqs_poll();
    CHECK(first_note() == 72 && seq_owner == 0, "LOAD 1: back to its own (72)");

    press(PA_SOUND, 2);
    CHECK(first_note() == 72 && seq_owner == 0 && cur_slot == 2, "SOUND 3: its sound, slot 1's sequencer plays on");
    press(PA_SAVE, 2);
    CHECK(pa_armed && area_note(2) == 60, "SAVE to 3 (its sound came alone): asks first, nothing written");
    press(PA_SAVE, 2);
    CHECK(area_note(2) == 72 && seq_owner == 2 && cur_slot == 2, "SAVE again: slot 3 gets the sound and this sequencer");

    seqs.pat[0].note[0] = 50;                   /* a change, not saved yet */
    seqs_dirty = 1;
    press(PA_SAVE, 4);
    CHECK(area_note(4) == 50 && seq_owner == 4 && bank.used[4] && !seqs_dirty, "SAVE to empty slot 5: the sequencer playing, with the change (50)");
    CHECK(area_note(2) == 72, "... slot 3, where it came from, not written: as it last saved (72)");

    /* playing: LOAD 1 is taken over where the next pattern starts */
    engine_set(&edit);
    engine_sync(0);
    engine_seq_play(1);
    run_frames(STEP_F * 4u);                    /* inside the pattern (16 steps) */
    press(PA_LOAD, 0);
    seqs_poll();
    CHECK(seq_swap == 1u && first_note() == 50 && seq_owner == 0, "LOAD 1 while playing: waits, 5's still plays");
    run_frames(STEP_F * 6u);
    seqs_poll();
    CHECK(seq_swap == 1u && first_note() == 50, "... still, mid-pattern");
    run_frames(STEP_F * 7u);                    /* past step 16 */
    CHECK(seq_swap == 2u && first_note() == 72 && seq.playing == 0u, "the next pattern: slot 1's, from its top");
    seqs_poll();
    CHECK(seq_swap == 0u, "the main loop follows");
    press(PA_LOAD, 2);
    engine_seq_play(0);
    run_frames(256u);
    seqs_poll();
    CHECK(seq_swap == 0u && first_note() == 72 && seq_owner == 2, "LOAD 3 while playing, then STOP: taken at once");

    /* LOAD does not save the one it replaces: a change not yet saved is let go */
    press(PA_LOAD, 0);
    seqs_poll();
    seqs.pat[0].note[0] = 40;
    seqs_dirty = 1;
    press(PA_LOAD, 2);
    seqs_poll();
    CHECK(area_note(0) == 72 && first_note() == 72 && seq_owner == 2, "LOAD 3 over slot 1's unsaved change: not saved, let go");

    /* a slot whose sound came in alone (the editor's store) has no sequencer: LOAD keeps the one playing */
    patch_save(6, &edit);
    seqs_dirty = 0;
    press(PA_LOAD, 6);
    seqs_poll();
    CHECK(seq_owner == 6 && first_note() == 72 && seqs_dirty == 1u, "LOAD 7 (none of its own): it takes the one playing");
    CHECK(seqs_write() == 0 && area_note(6) == 72, "... saved to it, as the app does when quiet");

    /* power-on again: slot 3's sound, slot 1's sequencer (gset) */
    memset(&seqs, 0, sizeof seqs);
    cur_slot = -1;
    seq_owner = -1;
    ui_init(2, 0);
    CHECK(cur_slot == 2 && seq_owner == 0 && first_note() == 72, "power-on: slot 3's sound with slot 1's sequencer");

    CHECK(store_load(store_buf, STORE_NOW) == 0 && (memcpy(&q, store_buf + sizeof bank, sizeof q), q.pat[0].note[0] == 60),
          "the store's own sequencer: as it was (60), through every save of the bank");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
