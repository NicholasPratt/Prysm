/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm: a synth module for the M-VAVE FM-1, built on Dinghy (zvenson/dinghy). The sound is engine.c
 * (docs/PRYSM-DESIGN.md); this file is the app: notes, the controls, the screen. Below it (platform.c): updates,
 * the USB rescue, USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play the engine, 4 voices; the notes go out over USB-MIDI too
 *   OCT- OCT+  the octave (kept in flash)
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes in play the engine, with their velocity (the keys: full velocity)
 *   pages      ALGORITHM / EDIT ENV LFO GLO FX: the page; SELECT: the row; KNOB1..4: its parameters (ui.c)
 *   HOME       the oscilloscope; SAVE: the patches
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "PRYSM"          /* the home, rescue and crash screens say this */
#define CV_MAX (240u * 132u)      /* the canvas: the OSC pages' six rows (ui.c) */
#include "platform.c"

#include "engine.c"                             /* the sound: voices, oscillators, modulators, envelopes */

/* ---- the audio ISR: the engine, timed (the CPU meter on screen: the share of the 5.8 ms a render may take) */
#ifndef DINGHY_HOST
#define RENDER_TICKS (5805u * FM1_TICKS_PER_US)
static volatile uint32_t cpu_now, cpu_max;      /* ticks of the last / the longest render */
#endif
/* the oscilloscope's capture: the output (before MASTER), while the scope is shown (ui.c) */
#define SCOPE_N 2048u                           /* frames; a power of two */
static int16_t scope_buf[2][SCOPE_N];
static volatile uint32_t scope_w;               /* frames written (the ISR's; wraps) */
static volatile uint8_t scope_on;

static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks(), t;
#endif
    engine_render(out, n);
    if (scope_on) {
        uint32_t i, w = scope_w;
        for (i = 0; i < n; i++, w++) {
            int32_t l = out[2u * i], r = out[2u * i + 1u];
            scope_buf[0][w & (SCOPE_N - 1u)] = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
            scope_buf[1][w & (SCOPE_N - 1u)] = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
        }
        scope_w = w;
    }
#ifndef DINGHY_HOST
    t = fm1_ticks() - t0;
    cpu_now = t;
    if (t > cpu_max)
        cpu_max = t;
#endif
}

static void play(uint32_t note, uint32_t vel) { engine_note_on_vel(note, vel); }
static void stop(uint32_t note) { engine_note_off(note); }

#define KEY0 53u                                /* key 0 = F3 */
#include "patches.c"                            /* the 8 patch slots */
#include "ui.c"                                 /* the pages, the PATCH screen */
#include "prysm_splash.h"                       /* the power-on picture (tools/gen_splash.py, assets/splash.jpg) */

/* ---- the splash: the picture (from flash, through the canvas in strips), the name under it; until 2.5 s have
 * passed or a key or button is pressed. (Host mode skips it unless built with -DPRYSM_SPLASH: the UI scripts
 * time their screenshots from power-on.) */
static void splash(void)
{
#if !defined(DINGHY_HOST) || defined(PRYSM_SPLASH)
    uint32_t y, t0, rel, strip = CV_MAX / SPLASH_W;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (y = 0; y < SPLASH_H; y += strip) {
        uint32_t h = SPLASH_H - y < strip ? SPLASH_H - y : strip;
        cv_begin(SPLASH_W, h, C_BLACK);
        memcpy(cv_px, SPLASH + y * SPLASH_W, SPLASH_W * h * 2u);
        cv_blit((240u - SPLASH_W) / 2u, 4u + y);
    }
    draw_text_box(0, 216, 240, &FONT_S, "PRYSM  v" APP_VER, C_ACC, 1);
    t0 = fm1_ms;
    while (fm1_ms - t0 < 2500u) {
        platform_poll();
        if (fm1_in.notes || fm1_input_edges(&rel))
            break;                              /* a key or a button: straight on (a key then plays) */
    }
#endif
}

static void app_main(void)
{
    struct { int8_t oct, slot; } set = {0, -1}; /* what survives a power-off: the octave, the last patch slot */
    uint32_t prev = 0, k, t_change = 0, dirty = 0;
    uint8_t held[27] = {0};                     /* the note each key plays */
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    engine_init();
    splash();
    ui_init(set.slot);                          /* the sound of the slot last saved or loaded */

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct);
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); play(held[k], 127); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        if (ui_input(b))                        /* the pages: the sound changed */
            engine_set(&edit);
        if (slot_moved) {                       /* a patch saved or loaded: remember its slot */
            slot_moved = 0;
            set.slot = (int8_t)cur_slot;
            dirty = 1;
            t_change = fm1_ms;
        }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
#if DINGHY_USB_LOG
            if (st == 0x9Fu && d1 == 0 && d2) { usblog_dump_start(); continue; }   /* DEBUG: tools/usblog.py */
#endif
            if ((st & 0xF0u) == 0x90u && d2) play(d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) stop(d1);
        }
#if DINGHY_USB_LOG
        usblog_pump();
#endif

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)                /* the key LEDs: what sounds, on the keys of this octave */
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);

        ui_draw();

        if (dirty && engine_quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
