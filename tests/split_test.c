/* SPDX-License-Identifier: GPL-3.0-only */
/* The keyboard split and the sequencer's parts on a computer, through the app's own code (ui.c) and the engine: below
 * the split a voice plays OSC 1 alone, from it OSC 2 alone, each side its octave; the four voices shared; LEARN; a
 * part muted, cleared; recording into one part while the other's notes in that lane play on; format 12 patches read
 * with the split off; each saved patch its own split.
 *   tests/run_engine.sh runs it */
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t blk[2u * 256u];
static int64_t loud(uint32_t frames)            /* the engine, frames on: how loud (the sum of |left|) */
{
    int64_t a = 0;
    uint32_t i;
    for (; frames >= 256u; frames -= 256u) {
        engine_render(blk, 256u);
        for (i = 0; i < 256u; i++) a += blk[2u * i] < 0 ? -blk[2u * i] : blk[2u * i];
    }
    return a;
}
static int32_t voice_of(uint32_t note)          /* the voice holding note (-1: none) */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].gate && voice[v].note == note) return (int32_t)v;
    return -1;
}
static uint32_t sounding(void)                  /* voices the sequencer started */
{
    uint32_t v, n = 0;
    for (v = 0; v < NV; v++) n += voice[v].gate;
    return n;
}

int main(void)
{
    patch_t p = PATCH_INIT;
    uint8_t old[PATCH_V12_SIZE];
    int32_t v;
    uint32_t k;
    printf("split checks\n");
    engine_init();
    edit = PATCH_INIT;
    edit.cho_mix = edit.dly_mix = edit.rev_mix = 0;
    edit.split = 60;                            /* C4 */
    edit.split_oct[0] = 1;                      /* -1 octave below */
    engine_set(&edit);

    engine_note_on(48);
    engine_note_on(72);
    v = voice_of(48);
    CHECK(v >= 0 && voice[v].omask == 1u && voice[v].shift == -12, "C3 (below C4): OSC 1 alone, an octave down");
    v = voice_of(72);
    CHECK(v >= 0 && voice[v].omask == 2u && voice[v].shift == 0, "C5 (from C4): OSC 2 alone, as played");
    engine_note_on(60);
    v = voice_of(60);
    CHECK(v >= 0 && voice[v].omask == 2u, "C4, the split key itself: OSC 2's side");
    engine_note_on(50);
    engine_note_on(76);
    CHECK(voice_of(76) >= 0 && sounding() == 4u, "a fifth note: still 4 voices in all, shared by both sides");
    engine_all_off();
    loud(44100u);

    edit.lvl1 = 0;                              /* OSC 1 silent: the low side silent, the high side heard */
    engine_set(&edit);
    engine_note_on(48);
    CHECK(loud(8192u) == 0, "OSC 1's level 0: a low note is silent (OSC 2 not in it)");
    engine_note_off(48);
    loud(44100u);
    engine_note_on(72);
    CHECK(loud(8192u) > 0, "... a high note sounds");
    engine_all_off();
    loud(44100u);
    edit.lvl1 = PATCH_INIT.lvl1;
    edit.split = 0;
    engine_set(&edit);
    engine_note_on(48);
    v = voice_of(48);
    CHECK(v >= 0 && voice[v].omask == 3u && voice[v].shift == 0, "split OFF: both oscillators, as played");
    engine_all_off();
    loud(44100u);

    split_learn = 1;                            /* LEARN: the next key sets it, not played */
    CHECK(ui_split_key(65) == 1 && edit.split == 65 && !split_learn, "LEARN, then F4: the split is F4");
    CHECK(ui_split_key(67) == 0 && edit.split == 65, "... the next key plays as usual");
    edit.split = 60;
    edit.split_oct[0] = 2;
    engine_set(&edit);

    /* the parts: a step of C3 (lane 0, part 1) and C5 (lane 1, part 2) */
    seqs_init();
    edit.arp_bpm = 80;
    engine_set(&edit);
    engine_sync(0);
    seqs.pat[0].note[0] = 48; seqs.pat[0].trig[0] = SEQ_NOTE;
    seqs.pat[0].xnote[0][0] = 72; seqs.pat[0].xvel[0][0] = 100;
    engine_seq_play(1);
    loud(2048u);
    CHECK(voice_of(48) >= 0 && voice_of(72) >= 0, "both parts play");
    engine_seq_play(0);
    loud(4096u);
    seqs.part_mute[1] = 1;
    engine_seq_play(1);
    loud(2048u);
    CHECK(voice_of(48) >= 0 && voice_of(72) < 0, "MUTE2: part 2's note rests, part 1's plays");
    engine_seq_play(0);
    loud(4096u);
    seqs.part_mute[1] = 0;

    /* recording part 2 into lane 0 at step 2, where lane 0 holds part 1's C3 at step 3: it plays on */
    seqs.pat[0].note[2] = 48; seqs.pat[0].trig[2] = SEQ_NOTE;
    engine_seq_mute(1u, 1u);                    /* (lane 0, a part 2 key recording) */
    engine_seq_play(1);
    loud(5513u * 2u + 2048u);
    CHECK(voice_of(48) >= 0, "a part 2 key recording into lane 0: part 1's note there plays on");
    engine_seq_mute(1u, 0u);                    /* (lane 0, a part 1 key) */
    engine_seq_play(0);
    loud(4096u);
    engine_seq_play(1);
    loud(2048u);
    CHECK(voice_of(48) < 0, "a part 1 key recording into lane 0: part 1's note rests (you hear the key)");
    engine_seq_play(0);
    engine_seq_mute(0, 0);
    loud(4096u);

    seq_row = 4u;                               /* CLR2 over ALL: part 2's notes go, part 1's stay */
    part_clear(2);
    CHECK(seqs.pat[0].trig[0] == SEQ_NOTE && seqs.pat[0].xvel[0][0] == 0 && seqs.pat[0].trig[2] == SEQ_NOTE,
          "CLR2: C5 gone, the C3s stay");
    part_clear(1);
    for (k = 0; k < 64u && seqs.pat[0].trig[k] == SEQ_REST; k++) ;
    CHECK(k == 64u, "CLR1: the C3s gone");

    edit.split = 65;                            /* saved with the patch: slot 4 split at F4, slot 5 not */
    edit.split_oct[1] = 3;
    bank.version = PATCH_VERSION;               /* (as the boot leaves it) */
    patch_save(3, &edit);
    edit.split = 0;
    edit.split_oct[1] = 2;
    patch_save(4, &edit);
    memset(&bank, 0, sizeof bank);              /* (a power-on: the store read back) */
    patches_load();
    CHECK(patch_get(3, &p) == 0 && p.split == 65 && p.split_oct[1] == 3, "slot 4 saved and read back: split F4, OCT2 +1");
    CHECK(patch_get(4, &p) == 0 && p.split == 0, "slot 5: split OFF");
    patch_get(3, &edit);
    engine_set(&edit);
    engine_note_on(72);
    v = voice_of(72);
    CHECK(v >= 0 && voice[v].omask == 2u && voice[v].shift == 12, "slot 4 loaded: its split plays");
    engine_all_off();
    p = PATCH_INIT;

    memcpy(old, &p, sizeof old);                /* a format 12 patch: the split off */
    memset(&p, 0x55, sizeof p);
    CHECK(patch_from(&p, old, sizeof old, 12u) == 0 && p.split == 0 && p.split_oct[0] == 2 && p.split_oct[1] == 2,
          "a format 12 patch: split OFF, the octaves as played");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
