/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm: a synth module for the M-VAVE FM-1, built on Dinghy (zvenson/dinghy). The sound is engine.c
 * (docs/PRYSM-DESIGN.md); this file is the app: notes, the controls, the screen. Below it (platform.c): updates,
 * the USB rescue, USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play the engine, 4 voices; the notes go out over USB-MIDI too
 *   OCT- OCT+  the octave (kept in flash)
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes in play the engine
 *   knobs      TEMPORARY test controls until the pages (test_knobs)
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "PRYSM"          /* the home, rescue and crash screens say this */
#include "platform.c"

#include "engine.c"                             /* the sound: voices, oscillators, modulators, envelopes */

/* ---- the audio ISR: the engine, timed (the CPU meter on screen: the share of the 5.8 ms a render may take) */
#ifndef DINGHY_HOST
#define RENDER_TICKS (5805u * FM1_TICKS_PER_US)
static volatile uint32_t cpu_now, cpu_max;      /* ticks of the last / the longest render */
#endif
static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks(), t;
#endif
    engine_render(out, n);
#ifndef DINGHY_HOST
    t = fm1_ticks() - t0;
    cpu_now = t;
    if (t > cpu_max)
        cpu_max = t;
#endif
}

static void play(uint32_t note) { engine_note_on(note); }
static void stop(uint32_t note) { engine_note_off(note); }

/* ---- the screen: the test readouts, the octave and the CPU (the pages come later) */
#define C_ACC RGB(180, 140, 255)                /* lilac, as the logo */
#define C_OFF RGB(90, 90, 90)
#define KEY0 53u                                /* key 0 = F3 */

/* ---- TEMPORARY test controls, until the pages: osc 1's carrier and mod 1 on the knobs, so the engine can be heard.
 *   SELECT  carrier bank     KNOB1  carrier position    KNOB2  mod 1 amount
 *   KNOB3   mod 1 ratio      KNOB4  mod 1 mode          ALGORITHM  mod 2 amount     PRESETS  osc 2 level */
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int test_knobs(patch_t *p)               /* 1: the patch changed */
{
    static int32_t mode_acc;
    int32_t d, changed = 0;
#define TURN(enc, field, lo, hi, step)                                                      \
    if ((d = fm1_enc_take(enc)) != 0) {                                                     \
        field = (uint8_t)clampi((int32_t)(field) + d * (step), lo, hi);                    \
        changed = 1;                                                                        \
    }
    TURN(ENC_SELECT, p->osc[0].bank[0], 0, (int32_t)PW_BANKS - 1, 1)
    TURN(ENC_KNOB1, p->osc[0].pos[0], 0, 127, 2)
    TURN(ENC_KNOB2, p->osc[0].amt[0], 0, 127, 2)
    TURN(ENC_KNOB3, p->osc[0].ratio[1], 0, (int32_t)PR_RATIOS - 1, 1)
    TURN(ENC_ALGORITHM, p->osc[0].amt[1], 0, 127, 2)
    TURN(ENC_PRESETS, p->lvl2, 0, 127, 2)
#undef TURN
    if ((d = fm1_enc_take(ENC_KNOB4)) != 0) {   /* the mode: one step per 3 detents, so it does not flicker */
        mode_acc += d;
        if (mode_acc >= 3 || mode_acc <= -3) {
            p->osc[0].mode[0] = (uint8_t)((p->osc[0].mode[0] + (mode_acc > 0 ? 1u : MODE_N - 1u)) % MODE_N);
            mode_acc = 0;
            changed = 1;
        }
    }
    return changed;
}

static char *put(char *s, const char *a) { while (*a) *s++ = *a++; *s = 0; return s; }
static char *num(char *s, uint32_t v)
{
    char b[10];
    uint32_t i = 0;
    do b[i++] = (char)('0' + v % 10u); while (v /= 10u);
    while (i) *s++ = b[--i];
    *s = 0;
    return s;
}

static void draw_test(const patch_t *p)
{
    char s[40], *e;
    uint32_t q = (uint32_t)p->osc[0].pos[0] * (PW_WAVES - 1u) * 256u / 127u;
    e = put(s, PW_BANK_NAME[p->osc[0].bank[0]]);
    e = put(e, " ");
    e = put(e, PW_WAVE_NAME[p->osc[0].bank[0]][(q + 128u) >> 8]);
    draw_text_box(0, 84, 240, &FONT_S, s, C_WHITE, 1);
    e = put(s, "MOD1 ");
    e = put(e, MODE_NAME[p->osc[0].mode[0]]);
    e = put(e, " x");
    e = put(e, PR_RATIO_NAME[p->osc[0].ratio[1]]);
    e = put(e, "  AMT ");
    num(e, p->osc[0].amt[0]);
    draw_text_box(0, 102, 240, &FONT_S, s, C_ACC, 1);
    e = put(s, "MOD2 AMT ");
    e = num(e, p->osc[0].amt[1]);
    e = put(e, "  OSC2 ");
    num(e, p->lvl2);
    draw_text_box(0, 120, 240, &FONT_S, s, C_OFF, 1);
}

static void draw_status(int32_t oct)
{
    char s[40] = "OCT +0", *e = s + 6;
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
#ifndef DINGHY_HOST
    e = put(e, "   CPU ");
    e = num(e, cpu_now * 100u / RENDER_TICKS);
    e = put(e, "% MAX ");
    e = num(e, cpu_max * 100u / RENDER_TICKS);
    put(e, "%");
#else
    (void)e;
#endif
    draw_text_box(0, 212, 240, &FONT_S, s, C_OFF, 1);
}

static void app_main(void)
{
    struct { int8_t oct; } set = {0};           /* what survives a power-off (add yours, up to 256 bytes) */
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_status = 0;
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0};                     /* the note each key plays */
    patch_t edit;
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    engine_init();
    edit = PATCH_INIT;

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_test(&edit);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct);
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); play(held[k]); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
#if DINGHY_USB_LOG
            if (st == 0x9Fu && d1 == 0 && d2) { usblog_dump_start(); continue; }   /* DEBUG: tools/usblog.py */
#endif
            if ((st & 0xF0u) == 0x90u && d2) play(d1);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) stop(d1);
        }
#if DINGHY_USB_LOG
        usblog_pump();
#endif

        if (test_knobs(&edit)) {                /* TEMPORARY: until the pages */
            engine_set(&edit);
            draw_test(&edit);
        }

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)                /* what sounds, on the keys of this octave */
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);

        if (set.oct != oct_drawn || fm1_ms - t_status > 500u) {      /* the octave, the CPU twice a second */
            oct_drawn = set.oct;
            t_status = fm1_ms;
            draw_status(set.oct);
        }

        if (dirty && engine_quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
