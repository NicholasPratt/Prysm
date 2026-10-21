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

/* the parameters: every knob of the sound's pages (not SETUP's, the sequencer's steps or its cursor) */
static const prm_t *web_prm(uint32_t i, uint32_t *pg_, uint32_t *row_, uint32_t *k_)
{
    uint32_t pg, r, k, n = 0;
    for (pg = 0; pg < PG_N; pg++)
        for (r = 0; PAGES[pg].osc != G && r < PAGES[pg].n; r++)
            for (k = 0; k < 4u; k++) {
                const prm_t *p = &PAGES[pg].rows[r].p[k];
                if (p->kind == K_NONE || p->kind == K_CURSOR || p->kind == K_SNOTE || p->kind == K_STRIG ||
                    p->kind == K_SSLIDE)
                    continue;
                if (n++ == i) {
                    *pg_ = pg; *row_ = r; *k_ = k;
                    return p;
                }
            }
    return 0;
}

static char web_txt[64];
WEB uint32_t web_nparams(void)
{
    uint32_t n = 0, a, b, c;
    while (web_prm(n, &a, &b, &c)) n++;
    return n;
}
/* parameter i: "page|row|label|knob|offset|max|min|kind" (offset: in patch_t) */
WEB const char *web_param(uint32_t i)
{
    uint32_t pg, r, k;
    const prm_t *p = web_prm(i, &pg, &r, &k);
    char *s = web_txt;
    if (!p) return "";
    s = put(put(put(put(put(s, PAGES[pg].name), "|"), PAGES[pg].rows[r].name), "|"), p->label);
    s = num(put(s, "|"), k);
    s = num(put(s, "|"), (uint32_t)(pval(pg, p) - (uint8_t *)&edit));
    s = num(put(s, "|"), p->max);
    s = num(put(s, "|"), p->kind == K_SLEN ? 1u : 0u);
    num(put(s, "|"), p->kind);
    return web_txt;
}
/* parameter i's value v as the FM-1 shows it (with the rest of the sound as it is: a wave's name needs its bank) */
WEB const char *web_fmt(uint32_t i, uint32_t v)
{
    static patch_t t;
    uint32_t pg, r, k;
    const prm_t *p = web_prm(i, &pg, &r, &k);
    uint8_t *at;
    if (!p) return "";
    t = edit;
    at = (uint8_t *)&t + (pval(pg, p) - (uint8_t *)&edit);
    *at = (uint8_t)v;
    fmt(web_txt, p, at);
    return web_txt;
}

/* where the screen is (the editor follows it, and moves it: ALGORITHM and SELECT by the difference) */
WEB uint32_t web_page(void) { return page; }
WEB uint32_t web_row(void) { return page < PG_N ? row[page] : 0u; }
WEB uint32_t web_npages(void) { return PG_N; }
/* the sequencer's steps: where their notes, triggers and slides are in patch_t */
WEB uint32_t web_seq_off(uint32_t which)
{
    return which == 0 ? __builtin_offsetof(patch_t, seq_note) : which == 1 ? __builtin_offsetof(patch_t, seq_trig)
                                                                           : __builtin_offsetof(patch_t, seq_slide);
}
