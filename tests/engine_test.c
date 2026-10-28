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

/* render: notes held from on_ms to off_ms, then until end_ms; clip = how many samples hit full scale.
 * With p2: that patch from change_ms on (a knob turned while the notes sound) */
static const patch_t *render_p2;
static uint32_t render_change_ms, render_on_ms;   /* (render_on_ms: the notes start then, not at 0) */
static uint32_t render_vel = 127;               /* the notes' velocity */
static int render_arp;                          /* the notes into the arpeggiator (held), not played */
static void render(const char *name, const patch_t *p, const uint8_t *notes, uint32_t nn,
                   uint32_t off_ms, uint32_t end_ms)
{
    uint32_t total = end_ms * (FS / 1000u) / FRAMES * FRAMES, done = 0, k, clip = 0;
    int16_t *s = calloc(total * 2u, 2);
    memset(voice, 0, sizeof voice);
    engine_init();
    engine_set(p);
    if (!render_on_ms)
        for (k = 0; k < nn; k++) {
            if (render_arp) arp_key_down(notes[k], render_vel);
            else engine_note_on_vel(notes[k], render_vel);
        }
    while (done < total) {
        int32_t blk[2u * FRAMES] = {0};
        uint32_t i;
        if (done >= off_ms * (FS / 1000u) && done < off_ms * (FS / 1000u) + FRAMES)
            for (k = 0; k < nn; k++) {
                if (render_arp) arp_key_up(notes[k]);
                else engine_note_off(notes[k]);
            }
        if (render_p2 && done == render_change_ms * (FS / 1000u) / FRAMES * FRAMES)
            engine_set(render_p2);
        if (render_on_ms && done == render_on_ms * (FS / 1000u) / FRAMES * FRAMES)
            for (k = 0; k < nn; k++)
                engine_note_on(notes[k]);
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

    {                                           /* power-on: the ISR renders before app_main's engine_init */
        static int16_t s[2u * 44100u / 2u];
        uint32_t done = 0, total = 44100u / 2u / FRAMES * FRAMES, i;
        while (done < total) {
            int32_t blk[2u * FRAMES] = {0};
            if (done == 4u * FRAMES)
                engine_init();                  /* (no note played: it must stay silent) */
            engine_render(blk, FRAMES);
            for (i = 0; i < 2u * FRAMES; i++)
                s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
            done += FRAMES;
        }
        wav("boot", s, total);
        printf("%-14s %u frames\n", "boot", total);
    }

    p = solo();                                 /* a pure sine: 440 Hz */
    render("sine", &p, A4, 1, 900, 1000);
    engine_bend(16383, 12); render("bend_up", &p, A4, 1, 900, 1000);   /* pitch bend: full up, +-12: an octave */
    engine_bend(0, 2); render("bend_down", &p, A4, 1, 900, 1000);      /* full down, +-2: G4 */
    engine_bend(8192, 2);

    {                                           /* VCA OPEN: a chord let go drones on; OPEN at 0: it dies away */
        static const uint8_t CEG[] = {60, 64, 67};
        patch_t q;
        p = solo(); p.amp_r = 30; p.vca = 80;
        render("drone_chord", &p, CEG, 3, 300, 1500);
        p.vca = 0;
        render("drone_closed", &p, CEG, 3, 300, 1500);
        p.vca = 80;                             /* nothing played: OPEN up drones C3 at once */
        last_note = -1;                         /* (nothing played since power-on) */
        render("drone_instant", &p, CEG, 0, 300, 1500);
        {                                       /* opened after a note: on that note; AMT1 turned: heard at once */
            patch_t z = p, y;
            z.vca = 0; engine_set(&z);          /* (closed first: opening is what starts it) */
            last_note = 67;
            z.vca = 80; z.osc[0].amt[0] = 0;
            y = z; y.osc[0].amt[0] = 110;
            render_p2 = &y; render_change_ms = 700;
            render("drone_last", &z, CEG, 0, 300, 1400);
            render_p2 = 0;
            z.vca = 0; engine_set(&z);
        }
        q = p; q.vca = 0;                       /* OPEN back to 0 at 800 ms: it fades out (no click) */
        render_p2 = &q; render_change_ms = 800;
        render("drone_off", &p, CEG, 3, 300, 1500);
        render_p2 = 0;
    }

    p = solo();                                 /* FM: mod 1 at x1 into the carrier: harmonics of 440 */
    p.osc[0].amt[0] = 100;
    render("fm", &p, A4, 1, 900, 1000);

    p = solo();                                 /* RING: carrier x3 (1320) times mod 1 x1 (440): 880 and 1760 */
    p.osc[0].ratio[0] = 4;                      /* PR_RATIO 4 = x3 */
    p.osc[0].mode[0] = MODE_RING;
    p.osc[0].amt[0] = 127;
    render("ring", &p, A4, 1, 900, 1000);

    p = solo();                                 /* MIX: carrier 440 + mod 1 an octave up (880) added */
    p.mpitch[0][0] = 36;                    /* +12 (MIX: its pitch, not its ratio) */
    p.osc[0].ratio[1] = 0;                      /* (x0.5: not used in MIX) */
    p.osc[0].mode[0] = MODE_MIX;
    p.osc[0].amt[0] = 127;
    render("mix", &p, A4, 1, 900, 1000);
    p.mpitch[0][0] = 31;                    /* +7: a fifth, 659 Hz */
    render("mix_fifth", &p, A4, 1, 900, 1000);
    p.mpitch[0][0] = 24; p.mfine[0][0] = 127;   /* in tune, +50 cents: 453 Hz */
    render("mix_fine", &p, A4, 1, 900, 1000);
    p.mfine[0][0] = 64;                         /* mod envelope 2 on M1PITCH, held full: mod 1 up (as PITCH, below) */
    p.menv2_a = 0; p.menv2_s = 127; p.menv2_dest = DEST_M1PITCH; p.menv2_amt = 127;
    render("menv2_m1pitch", &p, A4, 1, 900, 1000);

    p = solo();                                 /* mod envelope 2 on PITCH, held full: the note up, as envelope 1 */
    p.menv2_a = 0; p.menv2_s = 127; p.menv2_dest = DEST_PITCH; p.menv2_amt = 127;
    render("menv2_pitch", &p, A4, 1, 900, 1000);
    p = solo();
    p.menv_a = 0; p.menv_s = 127; p.menv_dest = DEST_PITCH; p.menv_amt = 127;
    render("menv1_pitch", &p, A4, 1, 900, 1000);

    {                                           /* LFOSHAPE: the morph itself, then a square pulled down to a sine */
        uint32_t ph = 0x30000000u, ok = 1;
        int32_t t = lfo_wave(LFO_TRI, ph, 0), si = lfo_wave(LFO_SINE, ph, 0);
        ok &= lfo_morph(LFO_SINE, 32767 / 5 + 1, ph, 0) - t <= 200 && lfo_morph(LFO_SINE, 32767 / 5 + 1, ph, 0) - t >= -200;
        ok &= abs(lfo_morph(LFO_SINE, 32767 / 10, ph, 0) - (si + t) / 2) < 400;   /* halfway: between the two */
        ok &= lfo_morph(LFO_SQR, 32767, ph, 1234) == 1234;                         /* (past the end: S&H) */
        printf("  %s lfo morph: sine + a fifth of the way = triangle; halfway between; clamped at the end\n",
               ok ? "ok  " : "FAIL");
        if (!ok) return 1;
        p = solo();                             /* a slow square vibrato, M.ENV 2 holding it fully down: a sine */
        p.lfo_wave = LFO_SQR; p.lfo_rate = 40; p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 100;
        p.menv2_a = 0; p.menv2_s = 127; p.menv2_dest = DEST_LFOSHAPE; p.menv2_amt = 0;
        render("lfoshape_menv2", &p, A4, 1, 1400, 1500);
        p.lfo_wave = LFO_SINE; p.menv2_dest = DEST_OFF; p.menv2_amt = 64;
        render("lfoshape_sine", &p, A4, 1, 1400, 1500);
        p.lfo_wave = LFO_SQR; p.lfo2_dest = DEST_LFOSHAPE; p.lfo2_amt = 127; p.lfo2_rate = 70;   /* LFO 2 morphing it */
        render("lfoshape_lfo2", &p, A4, 1, 1400, 1500);
        p.lfo2_dest = DEST_OFF;
        render("lfoshape_sqr", &p, A4, 1, 1400, 1500);
    }

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
        p = saw_through(127, 0); p.hp_cut = 92; render("hp_880", &p, A2, 1, 900, 1000);   /* 883 Hz */
        p.hp_res = 127; render("hp_res", &p, A2, 1, 900, 1000);   /* HP RES: a peak at 883 Hz (soft-clipped) */
        p.osc[0].level = 16; render("hp_res_quiet", &p, A2, 1, 900, 1000);   /* (quiet: under the clip) */
        p.hp_cut = p.hp_res = 0; render("hp_quiet_ref", &p, A2, 1, 900, 1000);
        p = solo(); p.flt_type = FLT_COMBP; p.cut = 64; p.res = 127; p.key = 127; p.comb_damp = 0;
        render("comb_on", &p, A4, 1, 900, 1000);   /* a sine on A4, keytracked: 440 a peak */
        p.flt_type = FLT_COMBM; render("comb_neg", &p, A4, 1, 900, 1000);   /* COMB-: the peaks between: a notch */
        p.flt_type = FLT_COMBP; p.key = 0;     /* no keytrack: CUT alone */
        p.cut = 76; render("comb_abs", &p, A4, 1, 900, 1000);   /* MIDI 69: 440 a peak */
        p.cut = 80; render("comb_off", &p, A4, 1, 900, 1000);   /* MIDI 72 (523 Hz): 440 between */
        p = saw_through(64, 100); p.flt_type = FLT_COMBP; p.key = 127; p.comb_damp = 0;   /* a saw on A2, on the note */
        render("comb_even", &p, A2, 1, 1400, 1500);
        p.comb_spread = 127; render("comb_spread", &p, A2, 1, 1400, 1500);   /* the upper peaks off the harmonics */
        p = saw_through(64, 127); p.flt_type = FLT_COMBP; p.key = 127; p.comb_damp = 0; p.drive = 127;
        p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 127; p.glide = 90;   /* the worst: driven, swept, at full */
        p.lfo_dest[1] = DEST_SPREAD; p.lfo_amt[1] = 0; p.comb_spread = 64;
        p.menv_dest = DEST_CUTOFF; p.menv_amt = 127; p.menv_loop = 1; p.fenv = 127; p.flt_d = 50; p.flt_s = 0;
        {
            static const uint8_t LOW[] = {12, 100};
            render("comb_stable", &p, LOW, 2, 1500, 2000);
        }
        p = saw_through(64, 0); p.flt_type = FLT_COMBP; p.key = 127; p.comb_damp = 0;
        render("comb_res0", &p, A2, 1, 900, 1000);   /* the level as RES rises: broadband */
        p.res = 127; render("comb_res127", &p, A2, 1, 900, 1000);
        p.key = 0; p.cut = 70; render("comb_res127_off", &p, A2, 1, 900, 1000);   /* the peaks off the harmonics */
        p = saw_through(71, 0); p.flt_type = FLT_LP24; render("lp24_1k", &p, A2, 1, 900, 1000);
        p.res = 120; render("lp24_res", &p, A2, 1, 900, 1000);
        {                                       /* the top of RES: the oscillators cut at 30 ms, the filter sings on */
            static const uint8_t C3[] = {48}, C5[] = {72};
            patch_t q;
            p = saw_through(64, 127); p.key = 127; p.fenv = 64; p.amp_s = 127;
            q = p; q.lvl1 = q.lvl2 = 0;
            render_p2 = &q;
            render_change_ms = 30;
            render("self_c3", &p, C3, 1, 1400, 1500);
            render("self_c5", &p, C5, 1, 1400, 1500);
            p.flt_type = q.flt_type = FLT_LP24; render("self_lp24", &p, C5, 1, 1400, 1500);
            p.flt_type = q.flt_type = FLT_LP12; p.res = q.res = 110;
            render("self_res110", &p, C5, 1, 1400, 1500);   /* below the top: it rings, and dies */
            p.res = 127; p.lvl1 = p.lvl2 = 0;
            render_p2 = 0;
            render("self_silent", &p, C5, 1, 1400, 1500);   /* the oscillators at 0 from the start: it sings anyway */
            p.cut = 47;                         /* (low: about the note's own pitch, slow to grow) */
            render("self_silent_low", &p, C3, 1, 1400, 1500);
            render_p2 = 0;
        }
        p = saw_through(10, 127);               /* the worst: full resonance, drive, the envelope sweeping it */
        p.drive = 127; p.fenv = 127; p.flt_d = 40; p.flt_s = 0; p.amp_r = 30;
        render("stable", &p, A5, 1, 700, 1500);
        {                                       /* the cutoff knob jumps 30 -> 100 at 500 ms: it must glide */
            patch_t q = saw_through(100, 0);
            p = saw_through(30, 0);
            render_p2 = &q;
            render_change_ms = 500;
            render("cut_jump", &p, A2, 1, 900, 1000);
            render_p2 = 0;
        }
    }

    {                                           /* the LFO (amount 83 = +-1 semitone on PITCH; 127 = the most) */
        static const uint8_t A2[] = {45};
        p = solo(); p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 83; p.lfo_rate = 87;   /* 5 Hz vibrato */
        render("lfo_vib", &p, A4, 1, 1900, 2000);
        p.lfo_delay = 90;                       /* fades in over ~0.7 s */
        render("lfo_delay", &p, A4, 1, 1900, 2000);
        p = solo(); p.lfo_wave = LFO_SH; p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 127; p.lfo_rate = 70;   /* 2 Hz */
        render("lfo_sh", &p, A4, 1, 1900, 2000);
        p = saw_through(60, 30); p.lfo_dest[0] = DEST_CUTOFF; p.lfo_amt[0] = 127; p.lfo_rate = 70;
        render("lfo_cut", &p, A2, 1, 1900, 2000);
        p = solo(); p.lfo_dest[0] = DEST_AMP; p.lfo_amt[0] = 127; p.lfo_rate = 77;   /* 3 Hz tremolo */
        render("lfo_amp", &p, A4, 1, 1900, 2000);
        p = solo(); p.lfo2_dest = DEST_AMP; p.lfo2_amt = 127; p.lfo2_wave = LFO_SINE;   /* LFO 2 in time: 120 BPM */
        p.lfo2_sync = 5; p.arp_bpm = 80; render("l2_sync4", &p, A4, 1, 2900, 3000);   /* 1/4: 2 Hz */
        p.lfo2_sync = 9; render("l2_sync16", &p, A4, 1, 2900, 3000);                  /* 1/16: 8 Hz */
        p.lfo2_sync = 0; p.lfo2_rate = 77; render("l2_free", &p, A4, 1, 2900, 3000);  /* free: 2.88 Hz */
        p = solo(); p.lfo2_dest = DEST_LVL1; p.lfo2_amt = 127; p.lfo2_rate = 77;   /* the MIX page's level 1 */
        render("l2_lvl1", &p, A4, 1, 1900, 2000);
        p = saw_through(71, 0); p.lfo2_dest = DEST_RES; p.lfo2_amt = 127; p.lfo2_rate = 77;   /* resonance swept */
        render("l2_res", &p, (const uint8_t[]){45}, 1, 1900, 2000);
        p = solo(); p.lfo_wave = LFO_SQR; p.lfo_dest[0] = DEST_WAVE1; p.lfo_amt[0] = 127; p.lfo_rate = 57;   /* 1 Hz */
        render("lfo_wave1", &p, A4, 1, 1900, 2000);
        p = solo(); p.osc[0].amt[0] = 60; p.lfo_wave = LFO_SQR; p.lfo_dest[0] = DEST_MOD1; p.lfo_amt[0] = 127;
        p.lfo_rate = 57;
        render("lfo_mod1", &p, A4, 1, 1900, 2000);
        p = solo(); p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 100; p.lfo_rate = 60;   /* retrig: the same from a */
        p.lfo_retrig = 1;                       /* note at 0 ms and at 333 ms; not when it runs free */
        render("rt_on_0", &p, A4, 1, 1900, 2000);
        render_on_ms = 333; render("rt_on_333", &p, A4, 1, 1900, 2000); render_on_ms = 0;
        p.lfo_retrig = 0;
        render("rt_off_0", &p, A4, 1, 1900, 2000);
        render_on_ms = 333; render("rt_off_333", &p, A4, 1, 1900, 2000); render_on_ms = 0;
    }

    {                                           /* velocity (MIDI): the same note at 127 and at 40 */
        patch_t q = solo();
        q.osc[0].amt[0] = 55;                   /* some FM (moderate: less is then clearly darker) */
        q.cut = 110; q.vel_amp = 127; q.vel_cut = 0; q.vel_mod = 127;
        memset(voice, 0, sizeof voice); engine_init(); engine_set(&q);
        render_vel = 127; render("vel_127", &q, A4, 1, 900, 1000);
        render_vel = 40; render("vel_40", &q, A4, 1, 900, 1000);
        q.vel_amp = 0; q.vel_mod = 0;           /* no sensitivity: velocity changes nothing */
        render_vel = 40; render("vel_40_off", &q, A4, 1, 900, 1000);
        render_vel = 127;
    }

    {                                           /* the arpeggiator: C4 E4 G4 held, 120 BPM, 1/16 (125 ms) */
        static const uint8_t CEG[] = {60, 64, 67};
        patch_t q = solo();
        q.amp_r = 5; q.arp_on = 1; q.arp_bpm = 80; q.arp_div = DIV_16; q.arp_gate = 64;
        render_arp = 1;
        q.arp_mode = ARP_UP; render("arp_up", &q, CEG, 3, 1400, 1500);
        q.arp_mode = ARP_DOWN; render("arp_down", &q, CEG, 3, 1400, 1500);
        q.arp_mode = ARP_UPDN; render("arp_updn", &q, CEG, 3, 1400, 1500);
        q.arp_mode = ARP_UP; q.arp_oct = 1; render("arp_oct2", &q, CEG, 3, 1400, 1500);
        q.arp_oct = 0; q.arp_gate = 127; render("arp_tie", &q, CEG, 3, 1400, 1500);
        q.arp_gate = 64; q.arp_swing = 127; render("arp_swing", &q, CEG, 3, 1400, 1500);
        q.arp_swing = 0; q.arp_latch = 1; render("arp_latch", &q, CEG, 3, 300, 1500);   /* keys up at 300 ms */
        q.arp_latch = 0; render("arp_nolatch", &q, CEG, 3, 300, 1500);
        render_arp = 0;
    }

    {                                           /* the arp on the MIDI clock: START at frame 1000, ticks from 1100 at
                                                 * 120 BPM (918.75 frames); at 2 s the tempo drops to 100 BPM; STOP at
                                                 * 3.5 s, START again at 4 s. C4 E4 G4 held, 1/16 (6 ticks a step) */
        static const uint8_t CEG[] = {60, 64, 67};
        static int16_t s[2u * 44100u * 5u];
        uint32_t sw;
        for (sw = 0; sw < 2u; sw++) {
            patch_t q = solo();
            uint32_t done = 0, total = 44100u * 5u / FRAMES * FRAMES, k;
            double at = 1100.0, period = 918.75;
            int started = 0, stopped = 0, restarted = 0;
            FILE *f;
            q.amp_r = 5; q.arp_on = 1; q.arp_div = DIV_16; q.arp_gate = 64; q.arp_swing = sw ? 127 : 0;
            memset(voice, 0, sizeof voice);
            engine_init(); engine_set(&q); engine_sync(1);
            clk_r = clk.w; engine_frames = 0;
            mc.last_at = 0; mc.period_q8 = 0; mc.count = -1; mc.running = 0; mc.wait = -1;
            for (k = 0; k < 3u; k++) arp_key_down(CEG[k], 127);
            while (done < total) {
                int32_t blk[2u * FRAMES] = {0};
                uint32_t i;
                /* the events of the frames before this render (as the USB poll would have timed them) */
                if (!started && 1000u < done) { engine_clock(0xFA, 1000u); started = 1; }
                if (!stopped && 3.5 * 44100 < done) { engine_clock(0xFC, (uint32_t)(3.5 * 44100)); stopped = 1; }
                if (!restarted && 4.0 * 44100 < done) { engine_clock(0xFA, (uint32_t)(4.0 * 44100)); restarted = 1; at = 4.0 * 44100 + 100; }
                while (at < done) {
                    if (!(stopped && !restarted)) engine_clock(0xF8, (uint32_t)at);
                    at += period;
                    if (at > 2.0 * 44100 && period < 1000) period = 1102.5;   /* 100 BPM */
                }
                engine_render(blk, FRAMES);
                for (i = 0; i < 2u * FRAMES; i++)
                    s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
                done += FRAMES;
            }
            wav(sw ? "mclock_swing" : "mclock", s, total);
            if (!sw && (f = fopen("build/engine/mclock.txt", "w"))) { fprintf(f, "%u\n", mc_bpm10); fclose(f); }
            engine_sync(0);
        }
    }

    {                                           /* the sequencer: 120 BPM, 1/16 (125 ms a step), its own clock */
        static int16_t s[2u * 44100u * 3u];
        static const uint8_t PAT[8] = {48, 52, 55, 0, 60, 0, 55, 52};   /* C3 E3 G3 - C4 - G3 E3 (0: a rest) */
        uint32_t variant;
        for (variant = 0; variant < 10u; variant++) {   /* plain, length 3, accents, slide, no slide, tie, song, chord, a lane's tie */
            patch_t q = solo();
            pattern_t *pt = &seqs.pat[0];
            uint32_t done = 0, total = 44100u * 3u / FRAMES * FRAMES, k;
            static const char *const NAMES[10] = {"seq", "seq_len3", "seq_accent", "seq_slide", "seq_noslide", "seq_tie",
                                                  "seq_song", "seq_chord", "seq_lane_tie", "seq_click"};
            memset(voice, 0, sizeof voice);
            engine_init();                      /* (the patterns: empty) */
            q.amp_r = 5; q.arp_bpm = 80; seqs.div = DIV_16; seqs.gate = 64; pt->len = 8;
            q.vel_amp = 127;                    /* (accents: louder by velocity) */
            for (k = 0; k < 8u; k++) {
                pt->note[k] = PAT[k] ? PAT[k] : 48;
                pt->trig[k] = PAT[k] ? SEQ_NOTE : SEQ_REST;
                pt->slide[k] = 0;
            }
            if (variant == 1u) pt->len = 3;
            if (variant == 2u) { pt->trig[0] = SEQ_ACCENT; pt->trig[4] = SEQ_ACCENT; }
            if (variant == 3u || variant == 4u) {   /* two steps, C3 then G3, over and over; slide on the first */
                pt->len = 2; pt->note[0] = 48; pt->note[1] = 55;
                pt->trig[0] = pt->trig[1] = SEQ_NOTE;
                pt->slide[0] = variant == 3u;
            }
            if (variant == 5u) {                /* C3 tied over three steps, then a rest */
                pt->len = 4; pt->note[0] = 48;
                pt->trig[0] = SEQ_NOTE; pt->trig[1] = pt->trig[2] = SEQ_TIE; pt->trig[3] = SEQ_REST;
            }
            if (variant == 6u) {                /* the song: P1 (C3 C3) twice, P2 (G3 G3) once, round */
                pt->len = 2; pt->note[0] = pt->note[1] = 48; pt->trig[0] = pt->trig[1] = SEQ_NOTE;
                seqs.pat[1].len = 2; seqs.pat[1].note[0] = seqs.pat[1].note[1] = 55;
                seqs.pat[1].trig[0] = seqs.pat[1].trig[1] = SEQ_NOTE;
                seqs.mode = 1; seqs.song_len = 2;
                seqs.song_pat[0] = 0; seqs.song_rep[0] = 2; seqs.song_pat[1] = 1; seqs.song_rep[1] = 1;
            }
            if (variant == 7u) {                /* a chord: C4 E4 G4 on step 1 (the whole step), the rest rests */
                pt->len = 4; pt->trig[0] = SEQ_NOTE; pt->note[0] = 60; seqs.gate = 127;
                pt->trig[1] = pt->trig[2] = pt->trig[3] = SEQ_REST;
                pt->xnote[0][0] = 64; pt->xvel[0][0] = 100; pt->xnote[0][1] = 67; pt->xvel[0][1] = 100;
            }
            if (variant == 8u) {                /* lane 1's G4 tied over three steps; lane 0's C4 a step, then rests */
                pt->len = 4; pt->trig[0] = SEQ_NOTE; pt->note[0] = 60;
                pt->trig[1] = pt->trig[2] = pt->trig[3] = SEQ_REST;
                pt->xnote[0][0] = 67; pt->xvel[0][0] = 100;
                pt->xnote[1][0] = pt->xnote[2][0] = 67 | SEQ_XTIE; pt->xvel[1][0] = pt->xvel[2][0] = 100;
            }
            if (variant == 9u)                  /* the metronome alone: every step a rest */
                for (k = 0; k < 8u; k++) pt->trig[k] = SEQ_REST;
            engine_click(variant == 9u, 127, 0);
            engine_set(&q); engine_sync(0);
            last_note = -1;
            engine_seq_play(1);
            while (done < total) {
                int32_t blk[2u * FRAMES] = {0};
                uint32_t i;
                engine_render(blk, FRAMES);
                for (i = 0; i < 2u * FRAMES; i++)
                    s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
                done += FRAMES;
            }
            engine_seq_play(0);
            {                                   /* (a render to let it stop) */
                int32_t blk[2u * FRAMES] = {0};
                engine_render(blk, FRAMES);
            }
            wav(NAMES[variant], s, total);
        }
    }

    {                                           /* the sequencer on the MIDI clock: START at 0.5 s, 120 BPM */
        static int16_t s[2u * 44100u * 2u];
        patch_t q = solo();
        uint32_t done = 0, total = 44100u * 2u / FRAMES * FRAMES, k;
        double at = 22050.0 + 100.0;
        int started = 0;
        memset(voice, 0, sizeof voice);
        engine_init();
        q.amp_r = 5; seqs.div = DIV_16; seqs.gate = 64; seqs.pat[0].len = 4;
        for (k = 0; k < 4u; k++) { seqs.pat[0].note[k] = (uint8_t)(48 + 4 * k); seqs.pat[0].trig[k] = SEQ_NOTE; }
        engine_set(&q); engine_sync(1);
        clk_r = clk.w; engine_frames = 0;
        mc.last_at = 0; mc.period_q8 = 0; mc.count = -1; mc.running = 0; mc.wait = -1;
        engine_seq_play(1);
        while (done < total) {
            int32_t blk[2u * FRAMES] = {0};
            uint32_t i;
            if (!started && 22050u < done) { engine_clock(0xFA, 22050u); started = 1; }
            while (started && at < done) { engine_clock(0xF8, (uint32_t)at); at += 918.75; }
            engine_render(blk, FRAMES);
            for (i = 0; i < 2u * FRAMES; i++)
                s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
            done += FRAMES;
        }
        engine_seq_play(0);
        engine_sync(0);
        wav("seq_clock", s, total);
    }

    {                                           /* glide: C4 for 300 ms, then G4 (apart, or overlapping) */
        uint32_t mode;
        for (mode = 0; mode < 3u; mode++) {     /* ALWAYS apart; LEGATO apart; LEGATO overlapping */
            static int16_t s[2u * 44100u];
            patch_t q = solo();
            uint32_t done = 0, total = 44032u;
            q.glide = 70; q.glide_mode = mode ? GLIDE_LEGATO : GLIDE_ALWAYS; q.amp_r = 5;
            memset(voice, 0, sizeof voice);
            engine_init(); engine_set(&q);
            last_note = -1;
            engine_note_on(60);
            while (done < total) {
                int32_t blk[2u * FRAMES] = {0};
                uint32_t i;
                if (done == 13312u && mode < 2u) engine_note_off(60);     /* (300 ms) */
                if (done == 15360u) engine_note_on(67);                  /* (348 ms) */
                if (done == 17408u && mode == 2u) engine_note_off(60);
                engine_render(blk, FRAMES);
                for (i = 0; i < 2u * FRAMES; i++)
                    s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
                done += FRAMES;
            }
            wav(mode == 0u ? "glide_always" : mode == 1u ? "glide_legato_apart" : "glide_legato_over", s, total);
        }
    }

    {                                           /* the mod envelope */
        p = solo(); p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 83; p.lfo_rate = 50;   /* 0.7 Hz vibrato... */
        p.menv_dest = DEST_LFORATE; p.menv_amt = 127; p.menv_a = 105; p.menv_s = 127;  /* ... rising 4 octaves */
        render("menv_rate", &p, A4, 1, 2900, 3000);
        p = solo(); p.menv_dest = DEST_PITCH; p.menv_amt = 100; p.menv_a = 50; p.menv_d = 60; p.menv_s = 0;
        p.menv_r = 40; p.menv_loop = 1;         /* LOOP: attack-decay, again and again, until the release */
        p.amp_r = 90;                           /* (a long amp release: the pitch can be read after it) */
        render("menv_loop", &p, A4, 1, 1500, 2200);
        p.menv_loop = 0;
        render("menv_once", &p, A4, 1, 1500, 2200);
        p = solo(); p.lfo_dest[0] = DEST_PITCH; p.lfo_amt[0] = 83; p.lfo_rate = 87;   /* depth from the envelope */
        p.menv_dest = DEST_LFOAMT; p.menv_amt = 0; p.menv_a = 100; p.menv_s = 127;   /* -full: x0 at the top */
        render("menv_depth", &p, A4, 1, 2900, 3000);
    }

    {                                           /* the effects: a short blip (A4, 60 ms) into each */
        patch_t q;
        p = solo(); p.amp_r = 5;
        render("fx_dry", &p, A4, 1, 60, 1500);
        q = p; q.cho_mix = 0; q.dly_mix = 0; q.rev_mix = 0; q.cho_rate = 99; q.rev_size = 1; q.dly_time = 3;
        render("fx_bypass", &q, A4, 1, 60, 1500);   /* all mixes 0: the same as dry, whatever the rest */
        q = p; q.dly_mix = 127; q.dly_time = 34; q.dly_fb = 80; q.dly_ping = 0; q.dly_tone = 127;   /* ~200 ms */
        render("fx_dly", &q, A4, 1, 60, 1500);
        q = p; q.dly_mix = 0; q.dly_time = 34; q.dly_fb = 80; q.dly_tone = 127;   /* LFO 2 opens the delay's mix */
        q.lfo2_dest = DEST_DELAY; q.lfo2_amt = 127; q.lfo2_rate = 36; q.lfo2_wave = LFO_SINE;   /* 0.33 Hz: up first */
        render("l2_dly", &q, A4, 1, 60, 1500);
        q.dly_ping = 1;
        render("fx_ping", &q, A4, 1, 60, 1500);
        q.dly_ping = 0; q.dly_shift = 24; q.dly_fb = 110;   /* +12 st a repeat */
        render("fx_shift", &q, A4, 1, 60, 1500);
        q.dly_shift = 12; q.dly_wdepth = 127; q.dly_wrate = 77; q.dly_fb = 100;   /* wow: 3 Hz */
        p.amp_r = 5;
        {
            patch_t w = q;                      /* a held note: its repeats wobble */
            render("fx_wow", &w, A4, 1, 1400, 1500);
        }
        q = p; q.cho_mix = 127; q.cho_depth = 100; q.cho_rate = 60;
        render("fx_cho", &q, A4, 1, 1400, 1500);
        q = p; q.rev_mix = 127; q.rev_size = 40; q.rev_mdepth = 0;
        render("fx_rev_small", &q, A4, 1, 60, 3000);
        q.rev_size = 120;
        render("fx_rev_big", &q, A4, 1, 60, 3000);
        q.rev_mdepth = 0;
        render("fx_rev_held", &q, A4, 1, 2900, 3000);   /* a held note: still, then swept */
        q.rev_mdepth = 127; q.rev_mrate = 70;
        render("fx_rev_held_mod", &q, A4, 1, 2900, 3000);
        q.rev_mdepth = 0; q.rev_shimmer = 127; q.rev_size = 110;
        render("fx_shimmer", &q, A4, 1, 60, 3000);
        q = PATCH_INIT;                         /* everything at once, the worst: a chord into all of it */
        q.cho_mix = q.dly_mix = q.rev_mix = 127; q.rev_size = 127; q.rev_shimmer = 127; q.dly_fb = 127;
        q.dly_shift = 0; q.dly_wdepth = 127; q.rev_mdepth = 127;
        render("fx_all", &q, CHORD, 4, 1500, 4000);
    }

    p = PATCH_INIT;                             /* the default patch, a chord: four voices, no clipping */
    render("chord", &p, CHORD, 4, 1500, 2500);

    {                                           /* the noise, each type alone (osc 1 down), A4 for a second */
        static const char *const NAMES[NOISE_N] = {"noise_white", "noise_pink", "noise_brown", "noise_digi",
                                                   "noise_crackle", "noise_sh"};
        uint32_t t;
        for (t = 0; t < NOISE_N; t++) {
            p = solo(); p.lvl1 = 0;
            p.noise_type = (uint8_t)t; p.noise_lvl = 127; p.noise_col = NOISE_RATED(t) || t == NOISE_CRACKLE ? 64 : 127;
            render(NAMES[t], &p, A4, 1, 1000, 1100);
        }
        p.noise_type = NOISE_WHITE; p.noise_col = 30; render("noise_dark", &p, A4, 1, 1000, 1100);
        p.noise_type = NOISE_DIGI; p.noise_col = 64 + 16; render("noise_digi_up", &p, A4, 1, 1000, 1100);   /* +12 st */
    }
    {                                           /* the routing: a saw into a nearly closed filter, or DIRECT past it */
        p = saw_through(20, 0); render("route_filter", &p, A4, 1, 900, 1000);
        p.route[0] = ROUTE_DIRECT; render("route_direct", &p, A4, 1, 900, 1000);
        p.route[0] = ROUTE_FILTER; p.lvl1 = 0; p.noise_lvl = 127; p.noise_col = 127;   /* the noise: the same */
        render("route_nfilter", &p, A4, 1, 900, 1000);
        p.route[2] = ROUTE_DIRECT; render("route_ndirect", &p, A4, 1, 900, 1000);
    }
    {                                           /* the drums: 120 BPM, 1/16, 8 steps: kick on 1 and 5, snare on 3 and
                                                 * 7, the hat on every step (on 4 and 8 accented: open); 2 s, the
                                                 * sequencer stopped at 1 s (the tails after); then the snare alone on
                                                 * 3 and 7, flams, and plain hits */
        static int16_t s[2u * 44100u * 2u];
        static const char *const NAMES[8] = {"drums", "drums_crush", "drums_rev", "drums_midi", "drums_tune",
                                              "drums_filter", "drums_flam", "drums_snare"};
        uint32_t variant;
        for (variant = 0; variant < 8u; variant++) {
            patch_t q = solo();
            pattern_t *pt = &seqs.pat[0];
            uint32_t done = 0, total = 44100u * 2u / FRAMES * FRAMES, k;
            memset(voice, 0, sizeof voice);
            engine_init();
            q.arp_bpm = 80; seqs.div = DIV_16; pt->len = 8;
            if (variant >= 6u)
                for (k = 2; k < 8u; k += 4u) drum_put(pt, 1, k, variant == 6u ? 3u : 1u);
            else if (variant != 3u)
                for (k = 0; k < 8u; k++) {
                    if (k == 0u || k == 4u) drum_put(pt, 0, k, 1);
                    if (k == 2u || k == 6u) drum_put(pt, 1, k, 1);
                    drum_put(pt, 2, k, k == 3u || k == 7u ? 2u : 1u);
                }
            if (variant == 1u) seqs.dr_crush = 127;
            if (variant == 2u) { seqs.dr_rev = 127; q.rev_mix = 100; q.rev_size = 110; }
            if (variant == 4u) { seqs.dr_tune[0] = 36; seqs.dr_dec[0] = 40; }   /* the kick up an octave, short */
            if (variant == 5u) { seqs.dr_flt[1] = seqs.dr_flt[2] = 127; q.cut = 50; q.res = 60; }   /* snare, hat: a low cutoff */
            engine_click(0, 0, 0);              /* (no metronome) */
            engine_set(&q); engine_sync(0);
            engine_seq_play(variant != 3u);
            while (done < total) {
                int32_t blk[2u * FRAMES] = {0};
                uint32_t i;
                if (variant == 3u && done % 22016u == 0u && done < 44100u) engine_drum(DR_KICK_S, 127);   /* MIDI: every ~0.5 s */
                if (done == 44032u) engine_seq_play(0);
                engine_render(blk, FRAMES);
                for (i = 0; i < 2u * FRAMES; i++)
                    s[2u * done + i] = (int16_t)(blk[i] > 32767 ? 32767 : blk[i] < -32767 ? -32767 : blk[i]);
                done += FRAMES;
            }
            wav(NAMES[variant], s, total);
            printf("%-14s %u frames\n", NAMES[variant], total);
        }
    }

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
        if (getenv("PRYSM_FX")) {               /* (PRYSM_FX=1: the worst case with every effect on) */
            p.cho_mix = p.dly_mix = p.rev_mix = 127; p.dly_shift = 0; p.rev_shimmer = 127;
            p.dly_wdepth = p.rev_mdepth = 127;
        }
        if (getenv("PRYSM_NEW")) {              /* (PRYSM_NEW=1: and 1.4's: four routes, the flanger, CRUSH) */
            uint32_t i;
            for (i = 0; i < MM_SLOTS; i++) {
                p.mm_src[i] = (uint8_t)(MM_LFO + i); p.mm_dest[i] = (uint8_t)(i & 1u ? DEST_MOD1 : DEST_CUTOFF);
                p.mm_amt[i] = 100;
            }
            p.mfx_type = MFX_FLANGER; p.mfx_mix = 100; p.mfx_depth = 127; p.mfx_fb = 120;
            p.crush_bits = 60; p.crush_rate = 40;
        }
        p.hp_cut = 40; p.flt_type = getenv("PRYSM_LP24") ? FLT_LP24 : FLT_COMBP; p.res = 120; p.comb_spread = 100;   /* the high-pass, the comb, spread */
        p.lfo2_dest = DEST_RES; p.lfo2_amt = 127; p.lfo2_rate = 100;   /* LFO 2 on RES: the comb's, each block */
        p.osc[0].mode[1] = MODE_RING; p.osc[1].mode[0] = MODE_MIX;
        p.noise_lvl = 127; p.noise_type = NOISE_PINK; p.route[1] = ROUTE_DIRECT;   /* the noise, a route past the filter */
        memset(voice, 0, sizeof voice);
        engine_init();
        seqs.dr_drive = 100; seqs.dr_crush = 60; seqs.dr_dly = seqs.dr_rev = 127;   /* the four drums, always, half */
        seqs.dr_flt[0] = seqs.dr_flt[1] = seqs.dr_flt[2] = seqs.dr_flt[3] = 64;   /* through their filter (both ways) */
        engine_set(&p);
        engine_note_on(57); engine_note_on(60); engine_note_on(64); engine_note_on(67);
        t0 = clock();
        for (r = 0; r < reps; r++) {
            memset(blk, 0, sizeof blk);
            if (r % 8u == 0u) {
                engine_drum(DR_KICK_S, 127); engine_drum(DR_SNARE_S, 127); engine_drum(DR_OHAT_S, 127);
                engine_drum(DR_CLAP_S, 127);
            }
            engine_render(blk, FRAMES);
        }
        printf("worst case on this computer: %.1f us per 256-frame render (the FM-1 has 5805 us)\n",
               (double)(clock() - t0) / CLOCKS_PER_SEC * 1e6 / reps);
    }
    return 0;
}
