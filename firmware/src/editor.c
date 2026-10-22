/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor (web/editor/, the plugin): the sound's parameters set from outside, each by the byte of patch_t it lives
 * in, as if its knob were turned (clamped to the knob's range, its name and value in the pop-up); and the sequencer's
 * patterns and song (engine.c: seqs) by the byte of seqs_t, checked. The browser's emulator and the plugin call
 * ed_set / ed_seq_set directly; the FM-1 takes SysEx F0 7D 50 <type> ... F7 from the computer (capture.c passes them on):
 *   'H'                   hello: an editor is there. Answered by a dump (below); from then on the FM-1 tells it of
 *                         every change made on the FM-1 itself
 *   'D'                   a dump, again
 *   'V' o1 o0 v1 v0       patch byte o (o1 << 7 | o0) = v (v1 << 7 | v0); answered 'A' 'V' o0 0
 *   'U' o1 o0 v1 v0       the same for a byte of the patterns (seqs_t); answered 'A' 'U' o0 0
 * and sends back:
 *   'E' i <128 nibbles>   64 bytes of the current sound (patch_t) at i * 64
 *   'Q' i <128 nibbles>   64 bytes of the patterns (seqs_t) at i * 64
 *   'F' n1 n0 version s   the dump is complete (the patterns first, then this): n bytes of sound, the patch format, the
 *                         slot it came from (s: 0 none, 1 .. 8); then m1 m0 sv: m bytes of patterns, their format
 *   'C' o1 o0 v1 v0       patch byte o changed on the FM-1 (a knob, a patch loaded): now v
 *   'G' o1 o0 v1 v0       a byte of the patterns changed on the FM-1
 *   'N' s                 the current slot changed (0 none, 1 .. 8) */

static void ed_set(uint32_t off, uint32_t v)    /* patch byte off = v, as its knob would set it */
{
    const prm_t *p = 0;
    uint8_t *at = (uint8_t *)&edit + off;
    uint32_t pg, r, i, max = 127, min = 0;
    if (off >= sizeof edit)
        return;
    for (pg = 0; pg < PG_N && !p; pg++)        /* its knob: the parameter that edits this byte */
        for (r = 0; PAGES[pg].osc != G && r < PAGES[pg].n && !p; r++)
            for (i = 0; i < 4u; i++) {
                const prm_t *q = &PAGES[pg].rows[r].p[i];
                if (q->kind != K_NONE && q->kind != K_CURSOR && pval(pg, q) == at) {
                    p = q;
                    break;
                }
            }
    if (p) {
        max = p->max;
        min = p->kind == K_SLEN ? 1u : 0u;
    }
    v = v < min ? min : v > max ? max : v;
    if (*at == v)
        return;
    *at = (uint8_t)v;
    if (p) {
        char val[16];
        fmt(val, p, at);
        pop(p->label, val);
    }
    engine_set(&edit);
    dirty |= D_HDR | D_GFX | D_ROWS;
}

/* a byte of the patterns and song = v, if it is one a player can set and v is in its range; 1: set */
static int ed_seq_set(uint32_t off, uint32_t v)
{
    uint8_t *at = (uint8_t *)&seqs + off;
    uint32_t p0 = __builtin_offsetof(seqs_t, pat), pe = p0 + sizeof seqs.pat, lo = 0, hi = 0;
    if (off >= p0 && off < pe) {                /* a pattern: its length, notes, triggers, slides */
        uint32_t f = (off - p0) % sizeof(pattern_t);
        if (f == __builtin_offsetof(pattern_t, len)) lo = 1, hi = 64;
        else if (f >= __builtin_offsetof(pattern_t, note) && f < __builtin_offsetof(pattern_t, trig)) hi = 127;
        else if (f >= __builtin_offsetof(pattern_t, trig) && f < __builtin_offsetof(pattern_t, slide)) hi = SEQ_TRIGS - 1u;
        else if (f >= __builtin_offsetof(pattern_t, slide)) hi = 1;
        else return 0;                          /* (padding) */
    } else if (off == __builtin_offsetof(seqs_t, div)) hi = DIV_N - 1u;
    else if (off == __builtin_offsetof(seqs_t, gate)) hi = 127;
    else if (off == __builtin_offsetof(seqs_t, cur)) hi = SEQ_PATS - 1u;
    else if (off == __builtin_offsetof(seqs_t, mode)) hi = 1;
    else if (off == __builtin_offsetof(seqs_t, song_len)) lo = 1, hi = SONG_N;
    else if (off >= __builtin_offsetof(seqs_t, song_pat) && off < __builtin_offsetof(seqs_t, song_rep)) hi = SEQ_PATS - 1u;
    else if (off >= __builtin_offsetof(seqs_t, song_rep) && off < sizeof seqs) lo = 1, hi = 16;
    else
        return 0;                               /* (the version, padding, or past the end) */
    if (v < lo || v > hi)
        return 0;
    if (*at != v) {
        *at = (uint8_t)v;
        seqs_dirty = 1;                         /* (saved by the app, when quiet) */
        if (off == __builtin_offsetof(seqs_t, div) || off == __builtin_offsetof(seqs_t, gate))
            engine_set(&edit);                  /* (the step's timing) */
        if (page == PG_SEQ) dirty |= D_HDR | D_GFX | D_ROWS;
    }
    return 1;
}

#ifndef DINGHY_HOST
static uint8_t ed_on;                           /* an editor said hello: tell it of changes */
static patch_t ed_shadow;                       /* the sound as the editor knows it */
static seqs_t ed_sshadow;                       /* the patterns as the editor knows them */
static int32_t ed_slot = -2;

static void ed_chunks(uint8_t type, const uint8_t *b, uint32_t n)   /* n bytes as 64-byte chunks of nibbles */
{
    uint32_t i, j;
    uint8_t d[1u + 128u];
    for (i = 0; i < n; i += 64u) {
        d[0] = (uint8_t)(i / 64u);
        for (j = 0; j < 64u; j++) {
            uint8_t v = i + j < n ? b[i + j] : 0u;
            d[1u + 2u * j] = v >> 4;
            d[2u + 2u * j] = v & 15u;
        }
        cap_send(type, d, sizeof d, 0);
    }
}

static void ed_dump(void)
{
    uint8_t d[7];
    ed_chunks('Q', (const uint8_t *)&seqs, sizeof seqs);
    ed_chunks('E', (const uint8_t *)&edit, sizeof edit);
    d[0] = (uint8_t)(sizeof edit >> 7);
    d[1] = (uint8_t)(sizeof edit & 127u);
    d[2] = PATCH_VERSION;
    d[3] = (uint8_t)(cur_slot + 1);
    d[4] = (uint8_t)(sizeof seqs >> 7);
    d[5] = (uint8_t)(sizeof seqs & 127u);
    d[6] = SEQS_VERSION;
    cap_send('F', d, 7u, 0);
    ed_shadow = edit;
    ed_sshadow = seqs;
    ed_slot = cur_slot;
}

static void ed_sysex(const uint8_t *p, uint32_t n)   /* (from cap_sysex: p[2] is the type) */
{
    if (p[2] == 'H' || p[2] == 'D') {
        ed_on = 1;
        ed_dump();
    } else if (p[2] == 'V' && n == 7u) {
        uint32_t off = (uint32_t)p[3] << 7 | p[4];
        uint8_t a[3] = {'V', p[4], 0};
        ed_set(off, (uint32_t)p[5] << 7 | p[6]);
        if (off < sizeof edit)
            ((uint8_t *)&ed_shadow)[off] = ((const uint8_t *)&edit)[off];   /* (as set: clamped; no echo) */
        cap_send('A', a, 3u, 0);
    } else if (p[2] == 'U' && n == 7u) {
        uint32_t off = (uint32_t)p[3] << 7 | p[4];
        uint8_t a[3] = {'U', p[4], 0};
        a[2] = ed_seq_set(off, (uint32_t)p[5] << 7 | p[6]) ? 0u : 1u;
        if (off < sizeof seqs)
            ((uint8_t *)&ed_sshadow)[off] = ((const uint8_t *)&seqs)[off];
        cap_send('A', a, 3u, 0);
    }
}

static int ed_diff(uint8_t type, const uint8_t *now, uint8_t *shadow, uint32_t n, uint32_t *sent)
{
    uint32_t i;
    for (i = 0; i < n && *sent < 16u; i++)
        if (now[i] != shadow[i]) {
            uint8_t v = now[i], d[4] = {(uint8_t)(i >> 7), (uint8_t)(i & 127u), (uint8_t)(v >> 7), (uint8_t)(v & 127u)};
            if (cap_send(type, d, 4u, 0) != 0)
                return -1;
            shadow[i] = v;
            (*sent)++;
        }
    return 0;
}

static void ed_poll(void)                       /* the main loop: changes made here, to the editor (a few a pass) */
{
    uint32_t sent = 0;
    if (!ed_on)
        return;
    if (ed_diff('C', (const uint8_t *)&edit, (uint8_t *)&ed_shadow, sizeof edit, &sent) != 0 ||
        ed_diff('G', (const uint8_t *)&seqs, (uint8_t *)&ed_sshadow, sizeof seqs, &sent) != 0) {
        ed_on = 0;                              /* (nobody reads: the editor has gone) */
        return;
    }
    if (ed_slot != cur_slot) {
        uint8_t d[1] = {(uint8_t)(cur_slot + 1)};
        cap_send('N', d, 1u, 0);
        ed_slot = cur_slot;
    }
}
#else
static void ed_poll(void) {}
#endif
