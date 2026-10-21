/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound's parameters, from the pages' own tables (ui.c): what an editor shows and sets, with the FM-1's names,
 * ranges and values (fmt). Included after prysm.c by the browser's FM-1 (emu/exports.c) and the plugin
 * (plugin/core/core.c). Every knob of the sound's pages: not SETUP's, the sequencer's steps or its cursor. */
static const prm_t *prm_get(uint32_t i, uint32_t *pg_, uint32_t *row_, uint32_t *k_)
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

static char prm_txt[64];
static uint32_t prm_n(void)
{
    static uint32_t n;
    uint32_t a, b, c;
    if (!n)
        while (prm_get(n, &a, &b, &c)) n++;
    return n;
}
/* parameter i: "page|row|label|knob|offset|max|min|kind" (offset: in patch_t) */
static const char *prm_info(uint32_t i)
{
    uint32_t pg, r, k;
    const prm_t *p = prm_get(i, &pg, &r, &k);
    char *s = prm_txt;
    if (!p) return "";
    s = put(put(put(put(put(s, PAGES[pg].name), "|"), PAGES[pg].rows[r].name), "|"), p->label);
    s = num(put(s, "|"), k);
    s = num(put(s, "|"), (uint32_t)(pval(pg, p) - (uint8_t *)&edit));
    s = num(put(s, "|"), p->max);
    s = num(put(s, "|"), p->kind == K_SLEN ? 1u : 0u);
    num(put(s, "|"), p->kind);
    return prm_txt;
}
/* parameter i's value v as the FM-1 shows it (with the rest of the sound as it is: a wave's name needs its bank) */
static const char *prm_fmt(uint32_t i, uint32_t v)
{
    static patch_t t;
    uint32_t pg, r, k;
    const prm_t *p = prm_get(i, &pg, &r, &k);
    uint8_t *at;
    if (!p) return "";
    t = edit;
    at = (uint8_t *)&t + (pval(pg, p) - (uint8_t *)&edit);
    *at = (uint8_t)v;
    fmt(prm_txt, p, at);
    return prm_txt;
}
/* the sequencer's steps: where their notes (0), triggers (1) and slides (2) are in patch_t */
static uint32_t prm_seq_off(uint32_t which)
{
    return which == 0 ? __builtin_offsetof(patch_t, seq_note) : which == 1 ? __builtin_offsetof(patch_t, seq_trig)
                                                                           : __builtin_offsetof(patch_t, seq_slide);
}
