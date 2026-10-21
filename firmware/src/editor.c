/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor (web/editor.html): the sound's parameters set from outside, each by the byte of patch_t it lives in, as
 * if its knob were turned (clamped to the knob's range, its name and value in the pop-up). The browser's emulator
 * calls ed_set directly; the FM-1 takes SysEx F0 7D 50 <type> ... F7 from the computer (capture.c passes them on):
 *   'H'                   hello: an editor is there. Answered by a dump (below); from then on the FM-1 tells it of
 *                         every change made on the FM-1 itself
 *   'D'                   a dump, again
 *   'V' o1 o0 v1 v0       patch byte o (o1 << 7 | o0) = v (v1 << 7 | v0); answered 'A' 'V' o0 0
 * and sends back:
 *   'E' i <128 nibbles>   64 bytes of the current sound (patch_t) at i * 64
 *   'F' n1 n0 version s   the dump is complete: n bytes, the patch format, the slot it came from (s: 0 none, 1 .. 8)
 *   'C' o1 o0 v1 v0       patch byte o changed on the FM-1 (a knob, a patch loaded): now v
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

#ifndef DINGHY_HOST
static uint8_t ed_on;                           /* an editor said hello: tell it of changes */
static patch_t ed_shadow;                       /* the sound as the editor knows it */
static int32_t ed_slot = -2;

static void ed_dump(void)
{
    uint32_t i, j, n = sizeof edit;
    uint8_t d[1u + 128u];
    for (i = 0; i < n; i += 64u) {
        d[0] = (uint8_t)(i / 64u);
        for (j = 0; j < 64u; j++) {
            uint8_t v = i + j < n ? ((const uint8_t *)&edit)[i + j] : 0u;
            d[1u + 2u * j] = v >> 4;
            d[2u + 2u * j] = v & 15u;
        }
        cap_send('E', d, sizeof d, 0);
    }
    d[0] = (uint8_t)(n >> 7);
    d[1] = (uint8_t)(n & 127u);
    d[2] = PATCH_VERSION;
    d[3] = (uint8_t)(cur_slot + 1);
    cap_send('F', d, 4u, 0);
    ed_shadow = edit;
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
    }
}

static void ed_poll(void)                       /* the main loop: changes made here, to the editor (a few a pass) */
{
    uint32_t i, sent = 0;
    if (!ed_on)
        return;
    for (i = 0; i < sizeof edit && sent < 16u; i++) {
        uint8_t v = ((const uint8_t *)&edit)[i];
        if (v != ((const uint8_t *)&ed_shadow)[i]) {
            uint8_t d[4] = {(uint8_t)(i >> 7), (uint8_t)(i & 127u), (uint8_t)(v >> 7), (uint8_t)(v & 127u)};
            if (cap_send('C', d, 4u, 0) != 0) {
                ed_on = 0;                      /* (nobody reads: the editor has gone) */
                return;
            }
            ((uint8_t *)&ed_shadow)[i] = v;
            sent++;
        }
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
