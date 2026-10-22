/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's sound as a library (core.c: the firmware's own engine, pages and patches, compiled for a computer), for the
 * plugin. The sound is made at the FM-1's rate, 44.1 kHz, in blocks of a multiple of 16 frames.
 *
 * Threads: the sound (prysm_render, _note, _set, _load, _clock ...) from one thread at a time; prysm_fmt and
 * prysm_param read the current patch and are for the GUI thread (a value being changed meanwhile reads as either). */
#ifndef PRYSM_CORE_H
#define PRYSM_CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PRYSM_RATE 44100u
#define PRYSM_BLOCK 16u                         /* prysm_render: n a multiple of this ... */
#define PRYSM_MAX_FRAMES 256u                   /* ... up to this */

void prysm_init(void);
uint32_t prysm_patch_size(void);
uint32_t prysm_patch_version(void);
const uint8_t *prysm_patch(void);               /* the current sound (patch_t), prysm_patch_size() bytes */
void prysm_set(uint32_t off, uint32_t v);       /* patch byte off = v, as its knob would set it (clamped) */
int prysm_load(const uint8_t *p, uint32_t n, uint32_t version);   /* a whole sound (an older format: converted) */
void prysm_note(uint32_t note, uint32_t vel);   /* vel 0: off; through the arpeggiator when it is on */
void prysm_bend(uint32_t v14, uint32_t range);  /* 0 .. 16383, 8192 none; +- range semitones */
void prysm_all_off(void);
void prysm_sync(int daw);                       /* the arp, sequencer and LFO 2 on the clock below (1) or their own */
void prysm_clock(uint32_t st, uint32_t frame_ahead);   /* 0xF8 tick, 0xFA start, 0xFC stop: so many frames into the next render */
void prysm_seq_play(int on);
void prysm_render(float *l, float *r, uint32_t n);

const uint8_t *prysm_seqs(void);               /* the sequencer's patterns and song (not part of the sound) */
uint32_t prysm_seqs_size(void);
uint32_t prysm_seqs_version(void);
int prysm_seq_set(uint32_t off, uint32_t v);    /* a byte of them, checked; 1: set */
int prysm_seqs_load(const uint8_t *p, uint32_t n, uint32_t version);
const char *prysm_seq_layout(void);             /* where each field is (emu/params.c) */

uint32_t prysm_nparams(void);
const char *prysm_param(uint32_t i);            /* "page|row|label|knob|offset|max|min|kind" */
const char *prysm_fmt(uint32_t i, uint32_t v);  /* as the FM-1 shows it */

#ifdef __cplusplus
}
#endif
#endif
