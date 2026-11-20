/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's six-channel USB audio input, on this computer (no hardware): usb.c's configuration descriptor walked
 * (each descriptor's length adds up to wTotalLength; the AC header's to its own; the terminal, the feature unit and
 * the format six channels; the endpoint big enough for a 48 kHz packet; the channels' names), then the ring: one
 * render's master and the app's two pairs in, packets out, every frame's six channels where they went in. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HALF_FRAMES 256u
#define FELUCCA_UAC 1
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"

static int fails;
#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
                           else { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const uint8_t *d = CFG_DESC, *ac = 0;
    uint32_t n = sizeof CFG_DESC, i, ok = 1, it_ch = 0, fmt_ch = 0, maxp = 0, ep = 0, fu_len = 0, names = 0, ac_len = 0;
    uint32_t sum = 0;
    printf("USB audio, six channels\n");
    CHECK(d[1] == 2 && (d[2] | d[3] << 8) == n, "the configuration: wTotalLength %u = its size %u", d[2] | d[3] << 8, n);
    for (i = 0; i < n && d[i]; i += d[i]) {
        const uint8_t *x = d + i;
        sum += x[0];
        if (x[1] == 0x24 && x[2] == 1 && !ac) { ac = x; ac_len = x[5] | x[6] << 8; }   /* (the first: AC header) */
        if (x[1] == 0x24 && x[2] == 2 && x[0] == 12) { it_ch = x[7]; names = x[10]; ok &= (x[8] | x[9] << 8) == 0; }
        if (x[1] == 0x24 && x[2] == 6) fu_len = x[0];
        if (x[1] == 0x24 && x[2] == 2 && (x[0] == 14 || x[0] == 11) && x[3] == 1) fmt_ch = x[4];
        if (x[1] == 5 && x[2] == 0x84) { maxp = x[4] | x[5] << 8; ep = x[3]; }
    }
    CHECK(i == n && sum == n, "every descriptor's length adds up to it (%u of %u)", sum, n);
    {
        uint32_t s = 0;
        const uint8_t *x = ac;
        while (x && x < d + n && x[1] == 0x24) { s += x[0]; x += x[0]; }
        CHECK(ac && s == ac_len, "the audio control interface's class descriptors: %u B, as its header says (%u)", s, ac_len);
    }
    CHECK(it_ch == 6 && fmt_ch == 6 && ok, "the input terminal and the format: 6 channels (%u, %u), no spatial places", it_ch,
          fmt_ch);
    CHECK(fu_len == 7u + 7u, "the feature unit: the master's controls and six channels' (%u B)", fu_len);
    CHECK(ep == 0x05 && maxp >= 49u * 12u && maxp <= 1023u, "EP4 IN isochronous async: %u B, a 49-frame packet of 6 x 16 bit "
          "(588) fits", maxp);
    {
        const uint8_t *s;
        uint16_t l;
        char got[6][12];
        uint32_t k, j, good = 1;
        static const char *const WANT[6] = {"Master L", "Master R", "U2 L", "U2 R", "U3 L", "U3 R"};
        for (k = 0; k < 6u; k++) {
            if (!get_desc(0x0300u | (names + k), &s, &l) || s[1] != 3 || s[0] != l) { good = 0; break; }
            for (j = 0; j + 1u < (l - 2u) / 2u + 1u && j < 11u; j++) got[k][j] = (char)s[2u + 2u * j];
            got[k][j] = 0;
            good &= !strcmp(got[k], WANT[k]);
        }
        CHECK(names == 4 && good, "the channels' names, strings 4 .. 9: Master L .. U3 R");
        CHECK(!get_desc(0x0300u | 10u, &s, &l), "... and no string 10");
    }
    {   /* the ring: a render's master (L = i, R = -i) and two pairs (i + 1000 .., i + 2000 ..) in, packets out */
        static int32_t out[2u * HALF_FRAMES];
        static uint32_t a[HALF_FRAMES], b[HALF_FRAMES], pk[UA_MAXF * UA_W];
        static const volatile uint32_t live = 3u;
        uint32_t f, got = 0, frames = 0, good = 1, r;
        uac_stem[0] = a; uac_stem[1] = b; uac_stem_live = &live;
        uac.alt = 1; usb.config = 1; uac.flowing = 1;
        uac_render_start();                             /* (primes: silence, then this render) */
        for (r = 0; r < 3u; r++) {
            for (f = 0; f < HALF_FRAMES; f++) {
                uint32_t v = r * HALF_FRAMES + f;
                out[2u * f] = (int32_t)v; out[2u * f + 1u] = -(int32_t)v;
                a[f] = (v + 1000u) | (uint32_t)(uint16_t)(-(int32_t)v - 1000) << 16;
                b[f] = (v + 2000u) | (v + 3000u) << 16;
            }
            if (r) uac_render_start();
            for (f = 0; f < HALF_FRAMES; f += 32u) uac_tap(out + 2u * f, 32u, f);
            while (ua_w - ua_r >= 46u + 80u) {          /* (packets out: what the band keeps) */
                uint32_t m = uac_packet(pk), k;
                for (k = 0; k < m; k++, frames++) {
                    const uint32_t *x = &pk[k * UA_W];
                    int16_t ml = (int16_t)(x[0] & 0xFFFF), mr = (int16_t)(x[0] >> 16);
                    if (!x[0] && !x[1] && !x[2]) continue;   /* (the silence ahead) */
                    good &= ml == (int16_t)got && mr == -(int16_t)got && (x[1] & 0xFFFF) == got + 1000u &&
                            (int16_t)(x[1] >> 16) == -(int16_t)got - 1000 && (x[2] & 0xFFFF) == got + 2000u &&
                            (x[2] >> 16) == got + 3000u;
                    got++;
                }
            }
        }
        CHECK(good && got > 400u && !uac.underruns && !uac.overruns, "the ring: %u frames out, each its six channels "
              "where they went in, in order", got);
        uac_stem_live = 0;
    }
    {   /* 48 kHz: the master and U2 the same sine, U3 not live: U2 resampled as the master is, frame for frame; U3 silence */
        static int32_t out[2u * HALF_FRAMES];
        static uint32_t a[HALF_FRAMES], b[HALF_FRAMES], pk[UA_MAXF * UA_W];
        static const volatile uint32_t live = 1u;
        uint32_t f, r, same = 1, quiet = 1, loud = 0, ph = 0;
        uac_stem[0] = a; uac_stem[1] = b; uac_stem_live = &live;
        uac.r48 = 1; uac.go = 0;
        ua_r = ua_w = 0;
        uac.underruns = uac.overruns = 0;
        for (r = 0; r < 6u; r++) {
            for (f = 0; f < HALF_FRAMES; f++, ph++) {
                int32_t v = (int32_t)((ph * 37u) % 2000u) * 10 - 10000;   /* (a saw: every tap counts) */
                out[2u * f] = v; out[2u * f + 1u] = -v;
                a[f] = (uint32_t)(uint16_t)v | (uint32_t)(uint16_t)(-v) << 16;
                b[f] = 0x12345678u;                     /* (not live: never heard) */
            }
            uac_render_start();
            for (f = 0; f < HALF_FRAMES; f += 32u) uac_tap(out + 2u * f, 32u, f);
            while (ua_w - ua_r >= 49u + 87u) {
                uint32_t m = uac_packet(pk), k;
                for (k = 0; k < m; k++) {
                    same &= pk[k * UA_W + 1u] == pk[k * UA_W];
                    quiet &= pk[k * UA_W + 2u] == 0u;
                    loud |= pk[k * UA_W] != 0u;
                }
            }
        }
        CHECK(same && quiet && loud && !uac.underruns && !uac.overruns, "48 kHz: U2 resampled as the master, frame for frame; "
              "U3, nothing pinned: silence");
        uac_stem_live = 0;
        uac.r48 = 0;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
