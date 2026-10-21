/* SPDX-License-Identifier: GPL-3.0-only */
/* An FM-1 in a browser: the app (firmware/src/prysm.c) runs unchanged on it, compiled to WebAssembly by emscripten
 * (emu/build.sh). platform.c includes this file instead of the hardware when DINGHY_WEB is set (with DINGHY_HOST:
 * everything else is as on a computer, tests/host.c). The page (web/editor/) presses keys, buttons and knobs through
 * emu/exports.c, reads the screen (fb, RGB565) and plays the sound (an AudioWorklet).
 *
 * Time: each platform_poll is a millisecond, as on the FM-1; the sound is made in its 256-frame blocks and handed to
 * the page, which plays it. When the app is far enough ahead of what has been heard, platform_poll sleeps (Asyncify:
 * the browser gets on with the page meanwhile), so the app runs in real time, paced by the sound card. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t host_enc[7];
static int32_t fm1_enc_take(uint32_t e) { int32_t d = e < 7u ? host_enc[e] : 0; if (e < 7u) host_enc[e] = 0; return d; }
static uint8_t web_led[48];                     /* the LEDs (key and button ids), for the page */
static void fm1_led_key(uint32_t id, int on) { if (id < sizeof web_led) web_led[id] = (uint8_t)(on != 0); }
static int32_t fm1_batt_raw = 600;              /* the battery (platform.c): on a computer, a charged one */
static uint32_t fm1_batt(void) { return 3u; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it */
static uint16_t fb[240 * 240];
static uint32_t fb_gen;                         /* counts changes: the page redraws when it moves */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fb_gen++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fb_gen++;
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* the settings sector and the store (the patches): in RAM; the page keeps them (localStorage) and puts them back
 * before the app starts */
static uint8_t host_store[256 + 4];
static uint32_t store_gen;                      /* counts saves: the page keeps a copy when it moves */
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    store_gen++;
    return 0;
}
#define STORE_MAX (0x1000u - 16u)
static uint8_t host_store2[STORE_MAX];
static uint32_t host_store2_n;
static int store_load(void *p, uint32_t n)
{
    if (!host_store2_n || n != host_store2_n) return -1;
    memcpy(p, host_store2, n);
    return 0;
}
static int store_save(const void *p, uint32_t n)
{
    if (n > STORE_MAX) return -1;
    memcpy(host_store2, p, n);
    host_store2_n = n;
    store_gen++;
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the sound, to the page: a block of 256 stereo frames (float, -1 .. 1) */
EM_JS(void, web_audio_out, (const float *p, uint32_t n), {
    Module.onAudio && Module.onAudio(HEAPF32.slice(p >> 2, (p >> 2) + n * 2));
});
EM_JS(int, web_ahead, (void), {                 /* frames made but not heard yet */
    return Module.ahead ? Module.ahead() : 0;
});
#define AHEAD_MAX 3072u                         /* (70 ms: the delay from a key to its sound, at most) */

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    static float f[2u * HALF_FRAMES];
    fm1_ms++;
    owed += FS;
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];
            f[i] = (float)(v > 32767 ? 32767 : v < -32767 ? -32767 : v) * (1.0f / 32768.0f);
        }
        web_audio_out(f, HALF_FRAMES);
        while ((uint32_t)web_ahead() > AHEAD_MAX)
            emscripten_sleep(4);                /* far enough ahead: let the page run, the sound play */
    }
}

int main(void)
{
    app_main();
    return 0;
}
