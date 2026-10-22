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
WEB int32_t web_slot(void) { return cur_slot; }
WEB int web_modified(void) { return modified(); }

/* the store and the settings: the page keeps them (and puts them back before main runs) */
WEB uint8_t *web_store(void) { return host_store2; }
WEB uint32_t web_store_len(void) { return host_store2_n; }
WEB void web_store_set_len(uint32_t n) { host_store2_n = n <= STORE_MAX ? n : 0u; }
WEB uint8_t *web_settings(void) { return host_store; }
WEB uint32_t web_settings_size(void) { return sizeof host_store; }
WEB uint32_t web_store_gen(void) { return store_gen; }

#include "params.c"                             /* the parameters (shared with the plugin) */
WEB uint32_t web_nparams(void) { return prm_n(); }
WEB const char *web_param(uint32_t i) { return prm_info(i); }
WEB const char *web_fmt(uint32_t i, uint32_t v) { return prm_fmt(i, v); }

/* where the screen is (the editor follows it, and moves it: ALGORITHM and SELECT by the difference) */
WEB uint32_t web_page(void) { return page; }
WEB uint32_t web_row(void) { return page < PG_N ? row[page] : 0u; }
WEB uint32_t web_npages(void) { return PG_N; }
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
