/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm: a synth module for the M-VAVE FM-1, built on Dinghy (zvenson/dinghy). The sound is engine.c
 * (docs/PRYSM-DESIGN.md); this file is the app: notes, the controls, the screen. Below it (platform.c): updates,
 * the USB rescue, USB-MIDI, USB audio, keys, knobs, LEDs, the screen, a settings sector.
 *
 *   keys       play the engine, 4 voices; the notes go out over USB-MIDI too (on the DRUM page: the drums' steps)
 *   OCT- OCT+  the octave (kept in flash)
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   notes in (on the SETUP page's channel, or any) play the engine with their velocity; the keys' notes
 *              go out on its OUT channel; the MIDI clock can drive the arpeggiator (timed as it arrives: midi_rt);
 *              channel 10 plays the drums (General MIDI's notes: 36 kick, 38 snare, 42 closed hat, 46 open)
 *   pages      ALGORITHM / EDIT ENV LFO GLO FX: the page; SELECT: the row; KNOB1..4: its parameters (ui.c)
 *   HOME       the oscilloscope; SAVE: the patches; PLAY: the sequencer; REC: its step entry; ARP (on its page):
 *              the arpeggiator on / off
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "PRYSM"          /* the home, rescue and crash screens say this */
#define CV_MAX (240u * 132u)      /* the canvas: the OSC pages' six rows (ui.c) */
#define APP_SYSEX cap_sysex       /* SysEx F0 7D 50 ... from the computer: patches to the slots, the editor (capture.c) */
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

/* the MIDI clock, timed: the USB poll (TIMER5) calls this as each clock / transport byte arrives; its frame is the
 * last render's start plus the time since (24 MHz ticks -> 44.1 kHz frames: x 147 / 80000) */
#ifndef DINGHY_HOST
static volatile uint32_t audio_ticks_at, audio_frames_at;
static void midi_rt(uint32_t st)
{
    engine_clock(st, audio_frames_at + (fm1_ticks() - audio_ticks_at) * 147u / 80000u);
}
#endif

static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks(), t;
    audio_ticks_at = t0;                        /* (where the audio stream is, for midi_rt) */
    audio_frames_at = engine_frames;
#endif
    engine_render(out, n);
    if (scope_on) {
        uint32_t i, w = scope_w;
        for (i = 0; i < n; i++, w++) {
            int32_t l = out[2u * i], r = out[2u * i + 1u];
            scope_buf[0][w & (SCOPE_N - 1u)] = (int16_t)clip15(l);
            scope_buf[1][w & (SCOPE_N - 1u)] = (int16_t)clip15(r);
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

static void play(uint32_t note, uint32_t vel)   /* a key or MIDI: to the arpeggiator while it is on (and the looper) */
{
    loop_note(note, vel);
    if (eg.arp_on) arp_key_down(note, vel);
    else engine_note_on_vel(note, vel);
}
static void stop(uint32_t note)
{
    loop_note(note, 0);
    if (eg.arp_on) arp_key_up(note);
    else engine_note_off(note);
}

#define KEY0 53u                                /* key 0 = F3 */
#include "patches.c"                            /* the 8 patch slots */
static uint8_t cap_on;                          /* capture.c: a take is being recorded */
static void cap_enc(uint32_t id, int32_t delta);
static int32_t cap_enc_take(uint32_t e)         /* the encoders as the UI takes them: into a take too */
{
    int32_t d = fm1_enc_take(e);
    if (d && cap_on) cap_enc(e, d);
    return d;
}
#define fm1_enc_take cap_enc_take
#include "ui.c"                                 /* the pages, the PATCH screen */
#include "capture.c"                            /* performance capture: a take to film on a computer */
#include "editor.c"                             /* the editor: parameters set over USB, changes reported back */
#ifdef DINGHY_WEB
#include "../../emu/exports.c"                  /* (in a browser: what the page calls) */
#endif
/* ---- the splash: rainbow static that thins and closes in as its specks fly together into PRYSM (FONT_P, twice its size),
 * which fades to white, the version under it; 2.6 s, or until a key or button is pressed. Drawn whole each frame,
 * in two strips of the canvas. (Host mode skips it unless built with -DPRYSM_SPLASH: the UI scripts time their
 * screenshots from power-on.) */
#define SPL_X 40                                /* the word: 5 letters of 16 x 32, doubled: 160 x 64 */
#define SPL_Y 74
#define SPL_FRAME_MS 33u
#define SPL_GATHER_MS 300u                      /* the specks start flying ... */
#define SPL_FLY_MS 900u                         /* ... take this long (each up to 300 ms late) ... */
#define SPL_DONE_MS 1600u                       /* ... and the word stands */
#define SPL_END_MS 2600u

static uint32_t spl_hash(uint32_t x)            /* a speck's own random numbers */
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

static uint16_t spl_hue(uint32_t h, uint32_t v) /* hue 0..1535 at brightness v 0..255 -> RGB565 */
{
    uint32_t f = h & 255u, r, g, b;
    switch ((h >> 8) % 6u) {
    case 0: r = 255; g = f; b = 0; break;
    case 1: r = 255 - f; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = f; break;
    case 3: r = 0; g = 255 - f; b = 255; break;
    case 4: r = f; g = 0; b = 255; break;
    default: r = 255; g = 0; b = 255 - f; break;
    }
    return RGB(r * v / 255u, g * v / 255u, b * v / 255u);
}

static int spl_ink(uint32_t letter, uint32_t gx, uint32_t gy)   /* the word's pixel (FONT_P's cell): set? */
{
    static const char WORD[] = "PRYSM";
    uint32_t gi = (uint32_t)WORD[letter] - FONT_P.first, bpr = (FONT_P.bw[gi] + 1u) / 2u;
    uint32_t v = FONT_P.data[FONT_P.off[gi] + gy * bpr + gx / 2u];
    return ((gx & 1u) ? (v & 15u) : (v >> 4)) != 0;
}

static void spl_frame(uint32_t t)               /* the screen at t ms */
{
    uint32_t strip, i, l, gx, gy, rng = spl_hash(t + 1u);
    uint32_t dens = t < SPL_GATHER_MS ? 65536u : t >= SPL_DONE_MS - 300u ? 0u :
                    65536u - (t - SPL_GATHER_MS) * 65536u / (SPL_DONE_MS - 300u - SPL_GATHER_MS);   /* the static */
    uint32_t rr = dens * 200u >> 16;            /* ... inside an ellipse round the word, closing on it */
    uint32_t ea = 80u + rr, eb = 32u + rr * 3u / 5u, eab = ea * ea * eb * eb;
    for (strip = 0; strip < 2u; strip++) {
        int32_t y0 = (int32_t)strip * 120;
        cv_begin(240, 120, C_BLACK);
        if (dens)
            for (i = 0; i < 120u * 60u; i++) {  /* 2 x 2 specks: a random colour and brightness, fewer and fewer */
                int32_t dx = (int32_t)(i % 120u) * 2 - 120, dy = (int32_t)(i / 120u) * 2 + y0 - (SPL_Y + 32);
                rng = rng * 1664525u + 1013904223u;
                if ((rng >> 16) < dens && (uint32_t)(dx * dx) * eb * eb + (uint32_t)(dy * dy) * ea * ea <= eab)
                    cv_rect((int32_t)(i % 120u) * 2, (int32_t)(i / 120u) * 2, 2, 2,
                            spl_hue((rng >> 5) % 1536u, 40u + (rng & 31u) * 6u));
            }
        for (i = 0, l = 0; l < 5u; l++)         /* the word's specks: each from somewhere, flying home */
            for (gy = 0; gy < FONT_P.h; gy++)
                for (gx = 0; gx < 16u; gx++) {
                    uint32_t h, late, p, e;
                    int32_t sx, sy, tx, ty, x, y;
                    if (!spl_ink(l, gx, gy))
                        continue;
                    h = spl_hash(i++);
                    late = h % 300u;
                    tx = SPL_X + (int32_t)(l * 32u + gx * 2u);
                    ty = SPL_Y + (int32_t)(gy * 2u);
                    sx = (int32_t)(h >> 8) % 240;
                    sy = (int32_t)(h >> 20) % 240;
                    p = t < SPL_GATHER_MS + late ? 0u : t - SPL_GATHER_MS - late;
                    p = p >= SPL_FLY_MS ? 1024u : p * 1024u / SPL_FLY_MS;
                    e = 1024u - (((1024u - p) * (1024u - p) >> 10) * (1024u - p) >> 10);   /* ease out: 1 - (1-p)^3 */
                    x = sx + (tx - sx) * (int32_t)e / 1024;
                    y = sy + (ty - sy) * (int32_t)e / 1024 - y0;
                    if (y < -2 || y >= 120)
                        continue;
                    if (e >= 1024u)
                        cv_rect(x, y, 2, 2, C_WHITE);
                    else {                      /* rainbow on the way, whiter as it nears home */
                        uint16_t c = spl_hue((h + t * 3u) % 1536u, 255u);
                        uint32_t w = e * e >> 12;   /* 0 .. 256 */
                        uint32_t r = (c >> 11) * 8u, g = (c >> 5 & 63u) * 4u, b = (c & 31u) * 8u;
                        r += (255u - r) * w >> 8; g += (255u - g) * w >> 8; b += (255u - b) * w >> 8;
                        cv_rect(x, y, 2, 2, RGB(r, g, b));
                    }
                }
        if (t >= SPL_DONE_MS && strip == 1u) {  /* the version, under the word; the battery under it */
            static const char V[] = "v" APP_VER;
            uint32_t b = fm1_batt();
            const char *w = b == 4u ? "USB POWER" : b == 0u ? "BATTERY LOW" : "";
            int32_t bw = 34 + (*w ? 10 + text_w(&FONT_S, w) : 0), bx = (240 - bw) / 2;
            cv_text((240 - text_w(&FONT_S, V)) / 2, 172 - y0, &FONT_S, V, t >= SPL_DONE_MS + 200u ? C_MID : C_FAINT);
            if (t >= SPL_DONE_MS + 200u) {
                cv_batt(bx, 200 - y0, b, 2);
                if (*w) cv_text(bx + 44, 200 - y0, &FONT_S, w, b == 0u ? C_BRIGHT : C_MID);
            }
        }
        cv_blit(0, (uint32_t)y0);
    }
}

static void splash(void)
{
#if !defined(DINGHY_HOST) || defined(PRYSM_SPLASH)
    uint32_t t0 = fm1_ms, next = 0, rel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    while (fm1_ms - t0 < SPL_END_MS) {
        uint32_t t = fm1_ms - t0;
        if (t >= next && t < SPL_DONE_MS + 400u) {   /* (then it stands still: nothing to redraw) */
            spl_frame(t);
            next = t + SPL_FRAME_MS;
        }
        platform_poll();
        if (fm1_in.notes || fm1_input_edges(&rel))
            break;                              /* a key or a button: straight on (a key then plays) */
    }
    lcd_sync();
#endif
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0;
    uint8_t held[27] = {0};                     /* the note each key plays */
    uint32_t bend = 8192;                       /* the last MIDI pitch bend (the range may change under it) */
    if (settings_load(&gset, sizeof gset) != 0 &&   /* the settings; else an older, shorter record: before the LIVE */
        settings_load(&gset, GSET_V4_SIZE) != 0 &&  /* knobs, before the sequencer's slot, before CLICK, before BEND; */
        settings_load(&gset, GSET_V3_SIZE) != 0 &&  /* the octave and the slot */
        settings_load(&gset, GSET_V2_SIZE) != 0 &&
        settings_load(&gset, GSET_V1_SIZE) != 0)
        settings_load(&gset, 2);
    if (gset.click > 2u) gset.click = 0;
    if (gset.click_lvl > 127u) gset.click_lvl = 80;
    if (gset.bend > 24u) gset.bend = 2;
    if (gset.oct < -2 || gset.oct > 2) gset.oct = 0;
    if (gset.midi_in > 16u) gset.midi_in = 0;
    if (gset.midi_out > 15u) gset.midi_out = 0;
    if (gset.clock > 1u) gset.clock = 0;
    if (gset.seq_slot < -1 || gset.seq_slot >= (int8_t)PATCH_SLOTS) gset.seq_slot = gset.slot;   /* (before 1.3: the sound's) */
    engine_init();
    engine_sync(gset.clock);
    engine_click(gset.click, gset.click_lvl, 0);
#ifndef DINGHY_HOST
    midi_rt_hook = midi_rt;                     /* the MIDI clock, timed as it arrives */
#endif
    splash();
    ui_init(gset.slot, gset.seq_slot);          /* the sound of the slot last saved or loaded, the sequencer too */
#ifdef DINGHY_HOST
    cap_restore();                              /* (tools/capture.py render: where the take started) */
#endif

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * gset.oct);
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        if (keys != prev) cap_keys(keys);
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            if (down >> k & 1u && ui_drum_keys()) {   /* (the DRUM page: a step, a drum; nothing played) */
                held[k] = 0xFF;
                ui_drum_key(k);
                continue;
            }
            if (down >> k & 1u && page == PG_LIVE && live_keys(k)) {   /* (LIVE: a pattern, a patch, a mute) */
                held[k] = 0xFF;
                live_key(k);
                continue;
            }
            if (up >> k & 1u && held[k] == 0xFFu)
                continue;
            if (down >> k & 1u && ui_split_key(base + k)) {   /* (the MIX page's LEARN: the split there) */
                held[k] = 0xFF;
                engine_set(&edit);
                continue;
            }
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                if (ui_seq_key(held[k], 127)) engine_set(&edit);   /* (REC: into the pattern, and heard) */
                play(held[k], 127);
                midi_send(0x90u | gset.midi_out, held[k], 100);
            }
            if (up >> k & 1u) {
                ui_seq_key_up(held[k]);
                stop(held[k]);
                midi_send(0x80u | gset.midi_out, held[k], 0);
            }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        cap_buttons(b);
        if (b & (1u << BTN_OCT_DOWN) && gset.oct > -2) { gset.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && gset.oct < 2) { gset.oct++; dirty = 1; t_change = fm1_ms; }
        if (ui_input(b))                        /* the pages: the sound changed */
            engine_set(&edit);
        if (slot_moved) {                       /* a patch saved or loaded: remember its slot */
            slot_moved = 0;
            gset.slot = (int8_t)cur_slot;
            gset.seq_slot = (int8_t)seq_owner;
            dirty = 1;
            t_change = fm1_ms;
        }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            if (st == 0xBFu && d1 == 119u) {    /* (tools/capture.py: a take starts or ends) */
                if (d2) cap_start(keys); else cap_stop();
                continue;
            }
            cap_midi(st, d1, d2);
            if ((st & 0xF0u) == 0x90u && (st & 0x0Fu) == 9u && d2 && engine_drum_note(d1) >= 0) {
                engine_drum((uint32_t)engine_drum_note(d1), d2);   /* (channel 10: the drums, whatever IN says) */
                continue;
            }
            if ((st & 0xE0u) == 0x80u && (st & 0x0Fu) == 9u && engine_drum_note(d1) >= 0)
                continue;                       /* (its note-off: nothing to stop) */
#if DINGHY_USB_LOG
            if (st == 0x9Fu && d1 == 0 && d2) { usblog_dump_start(); continue; }   /* DEBUG: tools/usblog.py */
#endif
            if (st < 0xF0u && gset.midi_in && (st & 0x0Fu) != gset.midi_in - 1u)
                continue;                       /* (another channel than the SETUP page's) */
            if ((st & 0xF0u) == 0x90u && d2 && ui_split_key(d1)) { engine_set(&edit); continue; }   /* (LEARN) */
            if ((st & 0xF0u) == 0x90u && d2) {
                if (ui_seq_key(d1, d2)) engine_set(&edit);
                play(d1, d2);
            }
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) { ui_seq_key_up(d1); stop(d1); }
            else if ((st & 0xF0u) == 0xE0u) bend = d1 | (uint32_t)d2 << 7;   /* pitch bend */
            else if ((st & 0xF0u) == 0xB0u && d1 == 1u) engine_wheel(d2);   /* the mod wheel (the mod matrix's WHEEL) */
        }
        engine_bend(bend, gset.bend);
#if DINGHY_USB_LOG
        usblog_pump();
#endif

        base = (uint32_t)(KEY0 + 12 * gset.oct);
        for (k = 0; k < NV; k++)                /* the key LEDs: what sounds, on the keys of this octave */
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        if (ui_drum_keys())                     /* (the DRUM page: the lane's steps) */
            lit = ui_drum_leds();
        else if (page == PG_LIVE)               /* (LIVE: the lowest octave's, the pattern and the patch) */
            lit = (lit & ~live_led_keys()) | live_leds();
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, gset.oct < 0);
        fm1_led_key(BTN_OCT_UP, gset.oct > 0);
        fm1_led_key(BTN_PLAY, eg.seq_play);     /* the sequencer runs */
        fm1_led_key(BTN_ARP, eg.arp_on);        /* the arpeggiator is on */
        fm1_led_key(BTN_REC, seq_rec);          /* step entry, or recording */

        ui_draw();
        ed_poll();                              /* (an editor connected: what changed here) */

        if (gset_dirty) {                       /* the SETUP page changed something: save it too */
            gset_dirty = 0;
            dirty = 1;
            t_change = fm1_ms;
        }
        if (dirty && engine_quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent */
            settings_save(&gset, sizeof gset);
            dirty = 0;
        }
        {                                       /* the patterns and the song: 3 s after the last change, when silent */
            static uint32_t seqs_at;            /* (the flash write would cut a note for ~50 ms) */
            if (seqs_dirty == 1u) { seqs_dirty = 2; seqs_at = fm1_ms; }
            if (seqs_dirty == 2u && engine_quiet() && fm1_ms - seqs_at > 3000u && seq_swap != 1u) {
                seqs_dirty = 0;
                seqs_write();                   /* (to whose it is: a slot's, or the store's own) */
            }
            if (live_poll())                    /* (LIVE: the bar line came, the patch waiting with it) */
                engine_set(&edit);
            seqs_poll();
        }
    }
}
