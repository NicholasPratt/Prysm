/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's sound as a library (prysm_core.h): the firmware itself (firmware/src/prysm.c, as tests/host.c runs it on a
 * computer), without its main loop. One per process: the firmware keeps its state in globals. */
#define main prysm_host_main                    /* (tests/host.c's: not this library's) */
#include "prysm.c"
#undef main
#include "../../emu/params.c"
#include "prysm_core.h"

void prysm_init(void)
{
    engine_init();
    edit = clean = PATCH_INIT;
    engine_set(&edit);
}

uint32_t prysm_patch_size(void) { return sizeof edit; }
uint32_t prysm_patch_version(void) { return PATCH_VERSION; }
const uint8_t *prysm_patch(void) { return (const uint8_t *)&edit; }
void prysm_set(uint32_t off, uint32_t v) { ed_set(off, v); }

int prysm_load(const uint8_t *p, uint32_t n, uint32_t version)
{
    patch_t t;
    if (patch_from(&t, p, n, version) != 0)     /* (an older format: converted, as the FM-1 converts its bank) */
        return -1;
    edit = clean = t;
    engine_set(&edit);
    return 0;
}

void prysm_note(uint32_t note, uint32_t vel)
{
    if (note > 127u) return;
    if (vel) play(note, vel > 127u ? 127u : vel);
    else stop(note);
}
void prysm_bend(uint32_t v14, uint32_t range) { engine_bend(v14, range); }
int prysm_drum(uint32_t note, uint32_t vel)
{
    int s = engine_drum_note(note);
    if (s < 0) return 0;
    if (vel) engine_drum((uint32_t)s, vel > 127u ? 127u : vel);
    return 1;
}
void prysm_all_off(void)
{
    uint32_t n;
    for (n = 0; n < 128u; n++) stop(n);
}
void prysm_sync(int daw) { engine_sync(daw ? 1u : 0u); }
void prysm_clock(uint32_t st, uint32_t frame_ahead) { engine_clock(st, engine_frames + frame_ahead); }
void prysm_seq_play(int on) { engine_seq_play(on ? 1u : 0u); }

void prysm_render(float *l, float *r, uint32_t n)
{
    static int32_t blk[2u * PRYSM_MAX_FRAMES];
    uint32_t i;
    n = n > PRYSM_MAX_FRAMES ? PRYSM_MAX_FRAMES : n - n % PRYSM_BLOCK;
    memset(blk, 0, n * 2u * sizeof blk[0]);
    engine_render(blk, n);
    for (i = 0; i < n; i++) {
        int32_t a = blk[2u * i], b = blk[2u * i + 1u];
        l[i] = (float)(a > 32767 ? 32767 : a < -32767 ? -32767 : a) * (1.0f / 32768.0f);
        r[i] = (float)(b > 32767 ? 32767 : b < -32767 ? -32767 : b) * (1.0f / 32768.0f);
    }
}

const uint8_t *prysm_seqs(void) { return (const uint8_t *)&seqs; }
uint32_t prysm_seqs_size(void) { return sizeof seqs; }
uint32_t prysm_seqs_version(void) { return SEQS_VERSION; }
int prysm_seq_set(uint32_t off, uint32_t v) { return ed_seq_set(off, v); }
int prysm_seqs_load(const uint8_t *p, uint32_t n, uint32_t version)
{
    if (n != (version == SEQS_VERSION ? sizeof seqs : version == 2u ? SEQS_V2_SIZE : version == 1u ? SEQS_V1_SIZE : 0u) ||
        seqs_from(p, version) != 0)
        return -1;                              /* (an older version: converted, as the FM-1 converts its store) */
    engine_set(&edit);
    return 0;
}
const char *prysm_seq_layout(void) { return prm_seq_layout(); }

uint32_t prysm_nparams(void) { return prm_n(); }
const char *prysm_param(uint32_t i) { return prm_info(i); }
const char *prysm_fmt(uint32_t i, uint32_t v) { return prm_fmt(i, v); }
