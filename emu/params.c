/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound's parameters, from the pages' own tables (ui.c): what an editor shows and sets, with the FM-1's names,
 * ranges and values (fmt). Included after prysm.c by the browser's FM-1 (emu/exports.c) and the plugin
 * (plugin/core/core.c). Every knob of the sound's pages: not SETUP's, the sequencer's steps or its cursor; a knob
 * with another parameter (RATIO's MOD1 / MOD2: PIT1 / PIT2 in MIX or RING) has both, the other just after it. */
static const prm_t *prm_get(uint32_t i, uint32_t *pg_, uint32_t *row_, uint32_t *k_)
{
    uint32_t pg, r, k, a, n = 0;
    for (pg = 0; pg < PG_N; pg++)
        for (r = 0; PAGES[pg].osc != G && r < PAGES[pg].n; r++)
            for (k = 0; k < 4u; k++)
                for (a = 0; a < 2u; a++) {
                    const prm_t *p = a ? prm_alt(pg, r, k) : &PAGES[pg].rows[r].p[k];
                    const uint8_t *at = !p || p->kind == K_NONE ? 0 : pval(pg, p);
                    if (!at || at < (const uint8_t *)&edit || at >= (const uint8_t *)&edit + sizeof edit)
                        continue;               /* (not a byte of the sound: a cursor, the patterns) */
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
/* the patterns and song (engine.c: seqs_t), where each field is: "pat|pattern size|len|note|trig|slide|div|gate|cur|
 * mode|song_len|song_pat|song_rep|size|patterns|entries" (the pattern fields within a pattern) */
static const char *prm_seq_layout(void)
{
    static const uint32_t L[16] = {
        __builtin_offsetof(seqs_t, pat), sizeof(pattern_t), __builtin_offsetof(pattern_t, len),
        __builtin_offsetof(pattern_t, note), __builtin_offsetof(pattern_t, trig), __builtin_offsetof(pattern_t, slide),
        __builtin_offsetof(seqs_t, div), __builtin_offsetof(seqs_t, gate), __builtin_offsetof(seqs_t, cur),
        __builtin_offsetof(seqs_t, mode), __builtin_offsetof(seqs_t, song_len), __builtin_offsetof(seqs_t, song_pat),
        __builtin_offsetof(seqs_t, song_rep), sizeof(seqs_t), SEQ_PATS, SONG_N};
    char *s = prm_txt;
    uint32_t i;
    for (i = 0; i < 16u; i++) s = num(i ? put(s, "|") : s, L[i]);
    return prm_txt;
}
