/* SPDX-License-Identifier: GPL-3.0-only */
/* Two-part demo sounds: each built from two factory tones (tones.c), a MODE (DUAL, SPLIT, LAYER), the parts' levels
 * and places, and the shared effects; written as patch files (patches/NAME.prysm: the editor's Load patch, the plugin's
 * window) and played, a few seconds each, into build/demos/<name>.wav and build/demos/all.wav to listen to.
 *   tools/make_demos.sh builds and runs it */
#include <sys/stat.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

typedef struct {
    const char *file, *rom_a, *rom_b;           /* (the tones by name) */
    uint8_t mode, split, lvl_a, lvl_b, pan_a, pan_b, cho, dly, rev, oct_a;
    const char *how;                               /* what it plays: see dm_play() */
} demo_t;
static const demo_t DEMOS[] = {
    {"1-split-bass-pad", "FM BASS", "WARM PAD", VM_SPLIT, 60, 127, 105, 64, 64, 50, 0, 40, 1, "split"},
    {"2-layer-epiano-strings", "E.PIANO", "STRINGS", VM_LAYER, 0, 127, 90, 50, 78, 70, 0, 35, 2, "chords"},
    {"3-layer-bell-glass", "DX BELL", "GLASS PAD", VM_LAYER, 0, 115, 110, 34, 94, 0, 30, 60, 2, "chords"},
    {"4-dual-bass-lead", "PLUCKBASS", "SAW LEAD", VM_DUAL, 0, 127, 100, 64, 76, 0, 45, 30, 1, "duet"},
    {"5-split-cello-brass", "CELLO", "BRASS", VM_SPLIT, 60, 120, 110, 50, 78, 40, 0, 45, 2, "split"},
    {"6-layer-choir-chimes", "AAH CHOIR", "CHIMES", VM_LAYER, 0, 120, 85, 64, 100, 60, 25, 70, 2, "chords"},
    {"7-dual-acid-drone", "ACID", "DARKDRONE", VM_DUAL, 0, 127, 85, 64, 64, 0, 50, 35, 1, "duet"},
    {"8-layer-kalimba-space", "KALIMBA", "SPACE", VM_LAYER, 0, 120, 95, 24, 104, 0, 55, 55, 2, "chords"},
};
#define NDEMO (sizeof DEMOS / sizeof DEMOS[0])

static int dm_rom_index(const char *name)
{
    uint32_t i;
    for (i = 0; i < TONE_ROM_N; i++)
        if (!strcmp(TONE_ROM[i].name, name)) return (int)i;
    fprintf(stderr, "no factory tone %s\n", name);
    exit(1);
}

#define SECS 6u
static int16_t dm_pcm[2u * 44100u * SECS], dm_all[2u * 44100u * SECS * NDEMO];
static uint32_t dm_all_n;
static void dm_wav(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = frames * 4u, v[] = {16, 0x00020001u, 44100u, 44100u * 4u, 0x00100004u};
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

static uint32_t dm_at;                             /* (frames rendered of this demo) */
static void dm_run_to(uint32_t ms)                 /* render up to ms into dm_pcm */
{
    static int32_t b[2u * BLK];
    uint32_t i, end = ms * 441u / 10u;
    while (dm_at + BLK <= end && dm_at + BLK <= 44100u * SECS) {
        memset(b, 0, sizeof b);
        engine_render(b, BLK);
        for (i = 0; i < 2u * BLK; i++) dm_pcm[2u * dm_at + i] = (int16_t)clip15(b[i]);
        dm_at += BLK;
    }
}
static void dm_on(uint32_t part, uint32_t n) { key_part = (uint8_t)part; engine_note_on_vel(n, 100); key_part = 0; }
static void dm_off(uint32_t part, uint32_t n) { engine_note_off_part(n, part); }
static void dm_chord(uint32_t part, const uint8_t *n, uint32_t k, uint32_t ms, uint32_t len)
{
    uint32_t i;
    dm_run_to(ms);
    for (i = 0; i < k; i++) dm_on(part, n[i]);
    dm_run_to(ms + len);
    for (i = 0; i < k; i++) dm_off(part, n[i]);
}
static void dm_play(const char *how)               /* a phrase for the mode */
{
    static const uint8_t CH_C[] = {60, 64, 67}, CH_AM[] = {57, 60, 64}, CH_F[] = {53, 57, 60}, CH_G[] = {55, 59, 62};
    if (!strcmp(how, "chords")) {               /* (LAYER: chords, both parts dm_on every note) */
        dm_chord(0, CH_C, 3, 0, 1200); dm_chord(0, CH_AM, 3, 1300, 1200); dm_chord(0, CH_F, 3, 2600, 1200);
        dm_chord(0, CH_G, 3, 3900, 1400);
    } else if (!strcmp(how, "split")) {         /* (SPLIT: a bass line below C4, chords above) */
        static const uint8_t HI1[] = {64, 67, 72}, HI2[] = {64, 69, 72}, HI3[] = {65, 69, 72}, HI4[] = {67, 71, 74};
        const uint8_t *hi[4] = {HI1, HI2, HI3, HI4}, lo[4] = {36, 45, 41, 43};
        uint32_t k, t = 0;
        for (k = 0; k < 4u; k++, t += 1350u) {
            dm_run_to(t);
            dm_on(0, hi[k][0]); dm_on(0, hi[k][1]); dm_on(0, hi[k][2]);
            dm_on(0, lo[k]);
            dm_run_to(t + 500u); dm_off(0, lo[k]);
            dm_run_to(t + 650u); dm_on(0, lo[k] + 12u);
            dm_run_to(t + 1150u); dm_off(0, lo[k] + 12u);
            dm_run_to(t + 1250u); dm_off(0, hi[k][0]); dm_off(0, hi[k][1]); dm_off(0, hi[k][2]);
        }
    } else {                                    /* (DUAL: a bass line dm_on A, a melody dm_on B, dm_at once) */
        static const uint8_t BASS[8] = {36, 36, 43, 36, 41, 41, 43, 43}, MEL[8] = {72, 74, 76, 79, 77, 76, 74, 71};
        uint32_t k, t;
        for (k = 0, t = 0; k < 8u; k++, t += 650u) {
            dm_run_to(t);
            dm_on(0, BASS[k]);
            if (!(k & 1u)) dm_on(1, MEL[k / 2u * 2u]);
            dm_run_to(t + 300u); dm_off(0, BASS[k]);
            dm_run_to(t + 600u);
            if (!(k & 1u)) { dm_off(1, MEL[k / 2u * 2u]); dm_on(1, MEL[k / 2u * 2u + 1u]); }
            else dm_off(1, MEL[(k - 1u) / 2u * 2u + 1u]);
        }
    }
    dm_run_to(SECS * 1000u);
}

int main(void)
{
    static patch_t p;
    static tone_t t;
    char path[256];
    uint32_t d, i;
    mkdir("patches", 0755);
    mkdir("build", 0755);
    mkdir("build/demos", 0755);
    for (d = 0; d < NDEMO; d++) {
        const demo_t *x = &DEMOS[d];
        FILE *f;
        uint8_t head[7] = {'P', 'R', 'Y', 'S', PATCH_VERSION, sizeof p >> 8, sizeof p & 255u};
        p = PATCH_INIT;
        tone_rom((uint32_t)dm_rom_index(x->rom_a), &t); tone_put(&p, 0, &t);
        tone_rom((uint32_t)dm_rom_index(x->rom_b), &t); tone_put(&p, 1, &t);
        p.vmode = x->mode;
        p.split = x->split;
        p.split_oct[0] = x->oct_a;
        p.pvol[0] = x->lvl_a; p.pvol[1] = x->lvl_b;
        p.ppan[0] = x->pan_a; p.ppan[1] = x->pan_b;
        p.cho_mix = x->cho; p.dly_mix = x->dly; p.rev_mix = x->rev;
        snprintf(path, sizeof path, "patches/%s.prysm", x->file);
        if (!(f = fopen(path, "wb"))) { perror(path); return 1; }
        fwrite(head, 1, sizeof head, f);
        fwrite(&p, 1, sizeof p, f);
        fclose(f);
        engine_all_off();                       /* (the last one's tails gone) */
        for (dm_at = 0; dm_at < 44100u; dm_at += BLK) { static int32_t b[2u * BLK]; memset(b, 0, sizeof b); engine_render(b, BLK); }
        edit = p;
        engine_init();
        engine_set(&edit);
        dm_at = 0;
        memset(dm_pcm, 0, sizeof dm_pcm);
        dm_play(x->how);
        snprintf(path, sizeof path, "build/demos/%s.wav", x->file);
        dm_wav(path, dm_pcm, dm_at);
        for (i = 0; i < 2u * dm_at; i++) dm_all[2u * dm_all_n + i] = dm_pcm[i];
        dm_all_n += dm_at;
        printf("patches/%s.prysm  (%s + %s, %s)\n", x->file, x->rom_a, x->rom_b, VM_NAME[x->mode]);
    }
    dm_wav("build/demos/all.wav", dm_all, dm_all_n);
    printf("build/demos/all.wav (%u s)\n", dm_all_n / 44100u);
    return 0;
}
