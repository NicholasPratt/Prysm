/* SPDX-License-Identifier: GPL-3.0-only */
/* Performance capture (tools/capture.py): while it is on, the FM-1 tells the computer what it starts from and then
 * everything played on it, so the computer can play the take again through the same code (tests/host.c) and film
 * the screen exactly, with the sound.
 *
 *   start / stop   MIDI in: CC 119 on channel 16 (B F 77 7F = start, value 0 = stop), from tools/capture.py
 *   to the computer, SysEx F0 7D 50 <type> ... F7 (7D: non-commercial; 50: "P"):
 *     'S' i1 i0 <128 nibbles>   the start state, 64 bytes a message (i: which 64), as cap_state() lays it out
 *     'T' n2 n1 n0              the state is complete: n bytes
 *     then each event, t2 t1 t0 first (ms since the start, 7 bits each):
 *     'K' 4 x 7 bits            the keys held (bit k: key k, 0 = F3)
 *     'B' 2 x 7 bits            buttons pressed (bit = button id)
 *     'N' id d                  an encoder turned d - 64 detents (as the UI took them)
 *     'M' st>>7 st&7F d1 d2     a MIDI message received
 *     'X'                       the take ends
 * Not captured: the MASTER knob, the MIDI clock (a take on CLOCK = MIDI plays on the internal tempo). */
static uint32_t cap_t0;

/* the state a take starts from, field by field (the same bytes on the FM-1 and on a computer) */
static uint8_t cap_seq_on;                      /* (the state's copy of eg.seq_play) */
#define CAP_F(x) {&(x), sizeof(x)}
static const struct { void *p; uint16_t n; } CAP_STATE[] = {
    CAP_F(bank.version), CAP_F(bank.used), CAP_F(bank.p), CAP_F(edit), CAP_F(clean), CAP_F(cur_slot), CAP_F(gset),
    CAP_F(page), CAP_F(row), CAP_F(pa_slot), CAP_F(pa_row), CAP_F(seq_cursor), CAP_F(cap_seq_on),
    CAP_F(seqs), CAP_F(seq_row), CAP_F(song_cursor)};   /* (seqs: the patterns and the song) */
#undef CAP_F
#define CAP_FIELDS (sizeof CAP_STATE / sizeof CAP_STATE[0])

#ifndef DINGHY_HOST
static int cap_send(uint8_t type, const uint8_t *d, uint32_t n, int timed)   /* d: 7-bit bytes; 0 = sent */
{
    uint8_t m[8u + 132u];
    uint32_t k = 0, t = fm1_ms - cap_t0, i;
    m[k++] = 0xF0; m[k++] = 0x7D; m[k++] = 0x50; m[k++] = type;
    if (timed) { m[k++] = (uint8_t)(t >> 14 & 127u); m[k++] = (uint8_t)(t >> 7 & 127u); m[k++] = (uint8_t)(t & 127u); }
    for (i = 0; i < n && k < sizeof m - 1u; i++)
        m[k++] = d[i] & 127u;
    m[k++] = 0xF7;
    return ota_wire_send(m, k);
}

static void cap_keys(uint32_t keys)
{
    uint8_t d[4] = {(uint8_t)(keys >> 21), (uint8_t)(keys >> 14), (uint8_t)(keys >> 7), (uint8_t)keys};
    if (cap_on) cap_send('K', d, 4u, 1);
}

static void cap_buttons(uint32_t b)
{
    uint8_t d[2] = {(uint8_t)(b >> 7), (uint8_t)b};
    if (cap_on && b) cap_send('B', d, 2u, 1);
}

static void cap_enc(uint32_t id, int32_t delta)
{
    while (cap_on && delta) {                   /* (more than 63 detents at once: in parts) */
        int32_t p = delta > 63 ? 63 : delta < -63 ? -63 : delta;
        uint8_t d[2] = {(uint8_t)id, (uint8_t)(p + 64)};
        cap_send('N', d, 2u, 1);
        delta -= p;
    }
}

static void cap_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t d[4] = {(uint8_t)(st >> 7), (uint8_t)(st & 127u), d1, d2};
    if (cap_on) cap_send('M', d, 4u, 1);
}

static void cap_part(uint8_t *d, uint32_t i)   /* the state's message i (d: its 64 bytes as nibbles), then cleared */
{
    d[0] = (uint8_t)(i >> 7);
    d[1] = (uint8_t)(i & 127u);
    cap_send('S', d, 2u + 128u, 0);
    memset(d + 2, 0, 128u);
}

static void cap_start(uint32_t keys)            /* the state (64 bytes a message, as nibbles), then the keys held */
{
    uint8_t d[2u + 128u];
    uint32_t f, j, n = 0;
    cap_t0 = fm1_ms;
    cap_seq_on = eg.seq_play;
    memset(d, 0, sizeof d);
    for (f = 0; f < CAP_FIELDS; f++)            /* (each message sent as it fills: no buffer for the whole state) */
        for (j = 0; j < CAP_STATE[f].n; j++, n++) {
            uint8_t v = ((const uint8_t *)CAP_STATE[f].p)[j];
            d[2u + 2u * (n % 64u)] = v >> 4;
            d[3u + 2u * (n % 64u)] = v & 15u;
            if (n % 64u == 63u) cap_part(d, n / 64u);
        }
    if (n % 64u) cap_part(d, n / 64u);          /* (the last, its rest zeros) */
    d[0] = (uint8_t)(n >> 14 & 127u);
    d[1] = (uint8_t)(n >> 7 & 127u);
    d[2] = (uint8_t)(n & 127u);
    cap_send('T', d, 3u, 0);
    cap_on = 1;
    cap_keys(keys);
}

static void cap_stop(void)
{
    if (cap_on) cap_send('X', 0, 0u, 1);
    cap_on = 0;
}

/* patches from the computer (tools/demo.py send), SysEx F0 7D 50 <type> ... F7 the other way:
 *   'P' i <128 nibbles>         64 bytes of a patch_t (i: which 64), into a staging patch
 *   'W' slot n1 n0 version      the staged patch (n bytes, format version) into the slot, saved
 *   'L' slot                    the slot's sound becomes the current one, as the PATCH screen's LOAD (no asking)
 * each is answered 'A' type i-or-slot rc (rc 0: done; 1: wrong size or format; 2: the save failed; 3: empty), so the
 * computer sends the next only then (the platform holds one frame at a time: one sent meanwhile is dropped) */
static patch_t cap_stage;
static void ed_sysex(const uint8_t *p, uint32_t n);   /* (editor.c: the editor's messages) */

static void cap_sysex(const uint8_t *p, uint32_t n)   /* p: the 7-bit bytes between F0 and F7 */
{
    uint8_t a[3] = {0, 0, 0};
    uint32_t i, at;
    if (n < 3u || p[1] != 0x50u)               /* (the shortest: the editor's hello, 7D 50 H) */
        return;
    a[0] = p[2];
    a[1] = n > 3u ? p[3] : 0u;
    if (p[2] == 'P' && n == 4u + 128u) {
        for (i = 0, at = p[3] * 64u; i < 64u && at + i < sizeof cap_stage; i++)
            ((uint8_t *)&cap_stage)[at + i] = (uint8_t)(p[4u + 2u * i] << 4 | (p[5u + 2u * i] & 15u));
    } else if (p[2] == 'W' && n == 7u) {
        if (p[3] >= PATCH_SLOTS || (p[4] << 7 | p[5]) != sizeof(patch_t) || p[6] != PATCH_VERSION)
            a[2] = 1;
        else if (patch_save(p[3], &cap_stage) != 0)
            a[2] = 2;
        else if ((int32_t)p[3] == cur_slot)
            clean = cap_stage;                  /* (the slot's sound changed: the current one now differs, or not) */
        dirty = D_ALL;
    } else if (p[2] == 'L' && n == 4u) {
        patch_t s;
        if (p[3] >= PATCH_SLOTS || patch_get(p[3], &s) != 0)
            a[2] = 3;
        else {
            edit = clean = s;
            cur_slot = p[3];
            slot_moved = 1;
            engine_set(&edit);
            dirty = D_ALL;
        }
    } else {
        ed_sysex(p, n);
        return;
    }
    cap_send('A', a, 3u, 0);
}
#else
/* on a computer: the take's start state from the file PRYSM_STATE names (tools/capture.py render) */
static void cap_keys(uint32_t keys) { (void)keys; }
static void cap_buttons(uint32_t b) { (void)b; }
static void cap_enc(uint32_t id, int32_t delta) { (void)id; (void)delta; }
static void cap_midi(uint8_t st, uint8_t d1, uint8_t d2) { (void)st; (void)d1; (void)d2; }
static void cap_start(uint32_t keys) { (void)keys; }
static void cap_stop(void) {}

#define CAP_STATE_MAX 6144u
static uint8_t cap_buf[CAP_STATE_MAX];
_Static_assert(4u + PATCH_SLOTS + (PATCH_SLOTS + 2u) * sizeof(patch_t) + 4u + sizeof(gset_t) + 4u +
               PG_N * 4u + 12u + sizeof(seqs_t) <= CAP_STATE_MAX, "the capture state fits its buffer");

/* the state into b (load: b into the state; b 0: neither), its length */
static uint32_t cap_state(uint8_t *b, int load)
{
    uint32_t f, n = 0;
    cap_seq_on = eg.seq_play;
    for (f = 0; f < CAP_FIELDS; f++) {
        if (b && load) memcpy(CAP_STATE[f].p, b + n, CAP_STATE[f].n);
        else if (b) memcpy(b + n, CAP_STATE[f].p, CAP_STATE[f].n);
        n += CAP_STATE[f].n;
    }
    if (b && load)
        engine_seq_play(cap_seq_on);
    return n;
}

static void cap_restore(void)
{
    const char *path = getenv("PRYSM_STATE"), *dump = getenv("PRYSM_STATE_DUMP");
    FILE *f;
    size_t n;
    if (dump && (f = fopen(dump, "wb"))) {     /* (the state now: the default, for a scripted take) */
        fwrite(cap_buf, 1, cap_state(cap_buf, 0), f);
        fclose(f);
    }
    if (!path) return;
    if (!(f = fopen(path, "rb"))) { perror(path); exit(1); }
    n = fread(cap_buf, 1, sizeof cap_buf, f);
    fclose(f);
    if (n < cap_state(0, 0)) { fprintf(stderr, "%s: too short (%zu bytes)\n", path, n); exit(1); }
    cap_state(cap_buf, 1);
    if ((path = getenv("PRYSM_SEQSTORE")) != 0) {   /* (a scripted take: each slot's own sequencer, and whose plays) */
        static uint8_t sq[sizeof seqs];
        uint32_t i, len;
        int32_t own;
        if (!(f = fopen(path, "rb"))) { perror(path); exit(1); }
        for (i = 0; i < PATCH_SLOTS; i++)
            if (fread(&len, 4, 1, f) == 1 && len == sizeof seqs && fread(sq, 1, len, f) == len)
                seqstore_save(i, sq, len);
        if (fread(&own, 4, 1, f) == 1) seq_owner = own;
        fclose(f);
    }
    engine_set(&edit);
    engine_sync(gset.clock);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    dirty = D_ALL;
}
#endif
