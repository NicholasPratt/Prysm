/* SPDX-License-Identifier: GPL-3.0-only */
/* What the page (web/editor/) calls in the browser's FM-1 (emu/wasm.c), included at the end of prysm.c. The
 * parameters are the pages' own tables (ui.c), so the editor shows every knob with the FM-1's names, ranges and
 * values (fmt), and sets them as the FM-1's knobs do (ed_set). */
#define WEB EMSCRIPTEN_KEEPALIVE

WEB uint16_t *web_fb(void) { return fb; }
WEB uint32_t web_fb_gen(void) { return fb_gen; }
WEB uint8_t *web_leds(void) { return web_led; }
WEB uint32_t web_ms(void) { return fm1_ms; }

WEB void web_key(uint32_t k, int down)          /* key k (0 = F3 .. 26 = G5) */
{
    if (k < 27u) fm1_in.notes = down ? fm1_in.notes | 1u << k : fm1_in.notes & ~(1u << k);
}
WEB void web_btn(uint32_t id) { if (id < 14u) host_edges |= 1u << id; }
WEB void web_enc(uint32_t id, int32_t d) { if (id < 7u) host_enc[id] += d; }
WEB void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mq_w - mq_r < 64u) {
        mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)d1; mq[mq_w % 64u][2] = (uint8_t)d2;
        mq_w++;
    }
}

/* the sound being played: its bytes (patch_t), set one at a time as a knob would, or whole (a patch from the FM-1,
 * a file) */
WEB uint8_t *web_patch(void) { return (uint8_t *)&edit; }
WEB uint32_t web_patch_size(void) { return sizeof edit; }
WEB uint32_t web_patch_version(void) { return PATCH_VERSION; }
WEB void web_set(uint32_t off, uint32_t v) { ed_set(off, v); }
WEB int web_load(const uint8_t *p, uint32_t n, int32_t slot)   /* a whole sound; slot: where it came from (-1) */
{
    if (n != sizeof edit) return -1;
    if (p != (const uint8_t *)&edit) memcpy(&edit, p, n);   /* (or written in place by the page) */
    clean = edit;
    cur_slot = slot >= 0 && slot < (int32_t)PATCH_SLOTS ? slot : -1;
    engine_set(&edit);
    dirty = D_ALL;
    return 0;
}
WEB uint8_t *web_scratch(void)                 /* room for a patch of an older format (a file), for web_load_from */
{
    static uint8_t b[PATCH_OLD_MAX];
    return b;
}
WEB int web_load_from(const uint8_t *p, uint32_t n, uint32_t version)   /* a sound of any format (patch_from) */
{
    patch_t t;
    if (patch_from(&t, p, n, version) != 0) return -1;
    edit = t;
    return web_load((const uint8_t *)&edit, sizeof edit, -1);
}
WEB void web_random(uint32_t pct, uint32_t sec, uint32_t seed)   /* RANDOM (random.c), as the PATCH screen's: UNDO's */
{                                               /* step first (a roll at a time: the editor's own Undo is one step) */
    ud_take(PA_RND_WHAT);
    patch_random(&edit, pct, sec < RS_N ? sec : RS_ALL, seed);
    engine_set(&edit);
    dirty = D_ALL;
}
WEB uint32_t web_tone_rom_n(void) { return TONE_ROM_N; }   /* the factory's tones; the user's after them */
WEB uint32_t web_tone_n(void) { return TONE_ROM_N + TONE_USER_N; }
WEB const char *web_tone_group(uint32_t g) { return g < TONE_ROM_N / 8u ? TONE_GROUP[g] : "USER"; }   /* (8 a group) */
static char web_tname[TONE_NAME];
WEB const char *web_tone_name(uint32_t i)      /* the tone library's tone i (the factory's, then the user's): */
{                                               /* its name, "" for an empty user tone */
    static tone_t t;
    return i < TONE_ROM_N + TONE_USER_N && tone_at(i, &t) == 0 ? (memcpy(web_tname, t.name, TONE_NAME), web_tname) : "";
}
WEB int web_tone_load(uint32_t i, uint32_t part)   /* tone i into part (0 A, 1 B), as the PATCH screen's TONE: UNDO's */
{                                               /* step first; one part playing: osc 2 quiet. 0 = done */
    static tone_t t;
    if (i >= TONE_ROM_N + TONE_USER_N || tone_at(i, &t) != 0) return -1;
    ud_take("TONE");
    tone_put(&edit, part & 1u, &t);
    if (edit.vmode == VM_SINGLE) edit.lvl2 = 0;
    engine_set(&edit);
    dirty = D_ALL;
    return 0;
}
WEB int web_undo(void)                          /* the PATCH screen's UNDO (again: redo); 0 = nothing to undo */
{
    if (!ud_ok) return 0;
    ud_swap();
    engine_set(&edit);
    dirty = D_ALL;
    return 1;
}
WEB int32_t web_slot(void) { return cur_slot; }
WEB int web_modified(void) { return modified(); }

/* the store and the settings: the page keeps them (and puts them back before main runs) */
WEB uint8_t *web_store(void) { return host_store2; }
WEB uint32_t web_store_len(void) { return host_store2_n; }
WEB void web_store_set_len(uint32_t n) { host_store2_n = n <= STORE_MAX ? n : 0u; }
WEB uint8_t *web_settings(void) { return host_store; }
WEB uint32_t web_settings_size(void) { return sizeof host_store; }
WEB uint32_t web_store_gen(void) { return store_gen; }
WEB uint8_t *web_seqstore(uint32_t i) { return host_seqs[i % SEQSTORE_N]; }   /* each slot's sequencer */
WEB uint32_t web_seqstore_len(uint32_t i) { return i < SEQSTORE_N ? host_seqs_n[i] : 0u; }
WEB void web_seqstore_set_len(uint32_t i, uint32_t n) { if (i < SEQSTORE_N) host_seqs_n[i] = n <= STORE_MAX ? n : 0u; }
WEB uint32_t web_tone_area(void) { return TONE_AREA; }   /* (the user tones: these two seqstores) */
WEB void web_tones_changed(void) { tone_user_h = 0xFFu; dirty = D_ALL; }   /* (set from outside: read again) */

#include "params.c"                             /* the parameters (shared with the plugin) */
WEB uint32_t web_nparams(void) { return prm_n(); }
WEB const char *web_param(uint32_t i) { return prm_info(i); }
WEB const char *web_fmt(uint32_t i, uint32_t v) { return prm_fmt(i, v); }

/* where the screen is (the editor follows it, and moves it: ALGORITHM and SELECT by the difference) */
WEB uint32_t web_page(void) { return page; }
WEB uint32_t web_row(void) { return page < PG_N ? row[page] : 0u; }
WEB uint32_t web_npages(void) { return PG_N; }
WEB const char *web_page_name(uint32_t i) { return i < PG_N ? PAGES[i].name : ""; }
/* the patterns and song (not part of the sound): their bytes, set one at a time (checked), their layout */
WEB uint8_t *web_seqs(void) { return (uint8_t *)&seqs; }
WEB uint32_t web_seqs_size(void) { return sizeof seqs; }
WEB uint32_t web_seqs_version(void) { return SEQS_VERSION; }
WEB int web_seq_set(uint32_t off, uint32_t v) { return ed_seq_set(off, v); }
WEB const char *web_seq_layout(void) { return prm_seq_layout(); }
WEB void web_seqs_loaded(void) { seqs_dirty = 1; engine_set(&edit); dirty = D_ALL; }   /* (written in place, whole) */
WEB uint32_t web_seq_entry(void) { return eg.seq_play && seqs.mode ? seq.entry : 0xFFu; }
WEB void web_seqs_init(void) { seqs_init(); seqs_dirty = 1; engine_set(&edit); dirty = D_ALL; }
WEB uint32_t web_seq_playing(void) { return eg.seq_play ? seq.playing_pat << 8 | seq.playing : 0xFFFFu; }
WEB int32_t web_oct(void) { return gset.oct; }
