/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's engine on a computer: render test patches to WAVs in build/engine/ for tests/check_engine.py,
 * and time the worst case.
 *   tests/run_engine.sh */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../firmware/src/engine.c"

#define FS 44100u
#define FRAMES 256u                             /* as the ISR renders */

static void wav(const char *name, const int16_t *s, uint32_t n)
{
    char path[256];
    uint32_t bytes = n * 4u, hdr[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    FILE *f;
    snprintf(path, sizeof path, "build/engine/%s.wav", name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(hdr, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, n, f);
    fclose(f);
}

/* render: notes held from on_ms to off_ms, then until end_ms; clip = how many samples hit full scale */
static void render(const char *name, const patch_t *p, const uint8_t *notes, uint32_t nn,
                   uint32_t off_ms, uint32_t end_ms)
{
    uint32_t total = end_ms * (FS / 1000u) / FRAMES * FRAMES, done = 0, k, clip = 0;
    int16_t *s = calloc(total * 2u, 2);
    memset(voice, 0, sizeof voice);
    engine_init();
    engine_set(p);
    for (k = 0; k < nn; k++)
        engine_note_on(notes[k]);
    while (done < total) {
        int32_t blk[2u * FRAMES] = {0};
        uint32_t i;
        if (done >= off_ms * (FS / 1000u))
            for (k = 0; k < nn; k++)
                engine_note_off(notes[k]);
        engine_render(blk, FRAMES);
        for (i = 0; i < 2u * FRAMES; i++) {
            int32_t v = blk[i];
            if (v > 32767 || v < -32767) clip++;
            s[2u * done + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        done += FRAMES;
    }
    wav(name, s, total);
    printf("%-14s %u frames%s\n", name, total, clip ? "  CLIPPED" : "");
    free(s);
}

static patch_t solo(void)                       /* osc 1 carrier only, a sine; osc 2 off; amp: fast, full sustain */
{
    patch_t p = PATCH_INIT;
    memset(p.osc[0].amt, 0, sizeof p.osc[0].amt);
    memset(p.osc[0].pos, 0, sizeof p.osc[0].pos);
    memset(p.osc[0].bank, 0, sizeof p.osc[0].bank);
    p.osc[0].ratio[0] = p.osc[0].ratio[1] = p.osc[0].ratio[2] = 1;   /* x1 */
    p.osc[0].att[0] = p.osc[0].att[1] = 0;
    p.osc[0].level = 127;
    p.lvl1 = 127;
    p.osc[1].level = 0;
    p.amp_a = 0; p.amp_d = 0; p.amp_s = 127; p.amp_r = 20;
    p.cut = 127; p.res = 0; p.fenv = 64; p.key = 0; p.drive = 0;   /* the filter open, out of the way */
    return p;
}

static patch_t saw_through(uint8_t cut, uint8_t res)   /* osc 1 a saw (BASIC, position 73), the filter as given */
{
    patch_t p = solo();
    p.osc[0].pos[0] = 73;
    p.cut = cut;
    p.res = res;
    return p;
}

int main(void)
{
    static const uint8_t A4[] = {69}, CHORD[] = {57, 60, 64, 67};
    patch_t p;
    uint32_t bank, pos;
    (void)system("mkdir -p build/engine");

    p = solo();                                 /* a pure sine: 440 Hz */
    render("sine", &p, A4, 1, 900, 1000);

    p = solo();                                 /* FM: mod 1 at x1 into the carrier: harmonics of 440 */
    p.osc[0].amt[0] = 100;
    render("fm", &p, A4, 1, 900, 1000);

    p = solo();                                 /* RING: carrier x3 (1320) times mod 1 x1 (440): 880 and 1760 */
    p.osc[0].ratio[0] = 4;                      /* PR_RATIO 4 = x3 */
    p.osc[0].mode[0] = MODE_RING;
    p.osc[0].amt[0] = 127;
    render("ring", &p, A4, 1, 900, 1000);

    p = solo();                                 /* MIX: carrier 440 + mod 1 x2 (880) added */
    p.osc[0].ratio[1] = 3;                      /* x2 */
    p.osc[0].mode[0] = MODE_MIX;
    p.osc[0].amt[0] = 127;
    render("mix", &p, A4, 1, 900, 1000);

    p = solo();                                 /* the series chain: mod 2 (x1) FM into mod 1 (x1) FM into the carrier */
    p.osc[0].amt[0] = 80;
    p.osc[0].amt[1] = 80;
    render("chain", &p, A4, 1, 900, 1000);

    p = solo();                                 /* mod 1's AR: amount rises over 300 ms (attack 80 = ~300 ms) */
    p.osc[0].amt[0] = 110;
    p.osc[0].att[0] = 80;
    render("modenv", &p, A4, 1, 900, 1000);

    p = solo();                                 /* amp ADSR: a ~140 ms attack (68), decay to half, release */
    p.amp_a = 68; p.amp_d = 70; p.amp_s = 64; p.amp_r = 70;
    render("adsr", &p, A4, 1, 1000, 2000);

    p = solo();                                 /* osc 2: a 5th up (semi 31 = +7), equal level: 440 + 659 */
    p.osc[1] = p.osc[0];
    p.lvl2 = 127;
    p.semi2 = 31;
    p.fine2 = 64;
    render("osc2", &p, A4, 1, 900, 1000);

    {                                           /* the filter: cutoff 71 = 987 Hz; a saw on A2 (110 Hz) */
        static const uint8_t A2[] = {45}, A5[] = {81};
        p = saw_through(127, 0); render("lp_open", &p, A2, 1, 900, 1000);
        p = saw_through(71, 0); render("lp_1k", &p, A2, 1, 900, 1000);
        p = saw_through(71, 120); render("lp_res", &p, A2, 1, 900, 1000);
        p = saw_through(127, 0); render("key_open_hi", &p, A5, 1, 900, 1000);
        p = saw_through(71, 0); p.key = 127; render("key_on_lo", &p, A2, 1, 900, 1000);
        p = saw_through(71, 0); p.key = 127; render("key_on_hi", &p, A5, 1, 900, 1000);
        p = saw_through(71, 0); p.key = 0; render("key_off_hi", &p, A5, 1, 900, 1000);
        p = saw_through(30, 0);                 /* filter envelope: +60 semitones, decaying to 0 */
        p.fenv = 127; p.flt_a = 0; p.flt_d = 75; p.flt_s = 0; p.flt_r = 60;
        render("fenv", &p, A2, 1, 900, 1000);
        p = solo(); p.drive = 127; render("drive", &p, A4, 1, 900, 1000);
        p = saw_through(10, 127);               /* the worst: full resonance, drive, the envelope sweeping it */
        p.drive = 127; p.fenv = 127; p.flt_d = 40; p.flt_s = 0; p.amp_r = 30;
        render("stable", &p, A5, 1, 700, 1500);
    }

    p = PATCH_INIT;                             /* the default patch, a chord: four voices, no clipping */
    render("chord", &p, CHORD, 4, 1500, 2500);

    for (bank = 0; bank < PW_BANKS; bank++) {   /* every wave of every bank, 250 ms each: to listen to */
        static int16_t all[PW_WAVES * 8u * 11025u * 2u];
        uint32_t at = 0;
        char name[32];
        for (pos = 0; pos <= 127u; pos += 18u) {   /* 8 steps: the waves themselves */
            uint32_t total = 11008u, done = 0;
            p = solo();
            p.osc[0].bank[0] = (uint8_t)bank;
            p.osc[0].pos[0] = (uint8_t)(pos > 127u ? 127u : pos);
            memset(voice, 0, sizeof voice);
            engine_init();
            engine_set(&p);
            engine_note_on(48);
            while (done < total) {
                int32_t blk[2u * FRAMES] = {0};
                uint32_t i;
                engine_render(blk, FRAMES);
                for (i = 0; i < 2u * FRAMES; i++)
                    all[2u * (at + done) + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
                done += FRAMES;
            }
            at += total;
        }
        snprintf(name, sizeof name, "bank-%s", PW_BANK_NAME[bank]);
        wav(name, all, at);
        printf("%-14s %u frames\n", name, at);
    }

    {                                           /* the worst case: 4 voices, every operator, both oscillators */
        int32_t blk[2u * FRAMES];
        clock_t t0;
        uint32_t r, reps = 4000;
        p = PATCH_INIT;
        p.osc[0].amt[0] = p.osc[0].amt[1] = p.osc[1].amt[0] = p.osc[1].amt[1] = 100;
        p.osc[0].mode[1] = MODE_RING; p.osc[1].mode[0] = MODE_MIX;
        memset(voice, 0, sizeof voice);
        engine_init();
        engine_set(&p);
        engine_note_on(57); engine_note_on(60); engine_note_on(64); engine_note_on(67);
        t0 = clock();
        for (r = 0; r < reps; r++) {
            memset(blk, 0, sizeof blk);
            engine_render(blk, FRAMES);
        }
        printf("worst case on this computer: %.1f us per 256-frame render (the FM-1 has 5805 us)\n",
               (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / reps);
    }
    return 0;
}
