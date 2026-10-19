/* SPDX-License-Identifier: GPL-3.0-only */
/* Prysm's effects, after the voices, in stereo: chorus -> delay -> reverb.
 *
 *   chorus   a short delay swept by a sine, left and right a quarter cycle apart
 *   delay    up to DLY_N samples (743 ms) a side; feedback through a tone low-pass and (SHIFT) a pitch shifter, so
 *            each repeat climbs or falls; ping-pong; wow: a sine sweeps its time (the repeats' pitch wobbles)
 *   reverb   Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997), its lengths scaled from 29761 Hz to
 *            44100 Hz: pre-delay, four input diffusers, a two-sided tank whose two allpasses are swept (RATE, DEPTH:
 *            the tail's wobble); shimmer: inside the tank, one side's feed is crossfaded with itself an octave up,
 *            so the tail climbs; the loop's gain stays the decay's (< 1): it can never run away
 *
 * Integers only: samples Q15 in int32, the lines int16 (written saturated), coefficients Q15. Each effect is
 * skipped while its mix is 0 (then the dry sound passes untouched). The lines are big: in the pool (ENGINE_POOL).
 * fx_set() (main loop) derives what fx_render() (the audio ISR) reads. */

#define CHO_N 2048u                             /* the chorus line: 46 ms */
#define DLY_N 32768u                            /* the delay lines: 743 ms a side */
#define SH_N 4096u                              /* a pitch shifter's line; its window SH_W (46 ms) */
#define SH_W 2048
#define PRE_N 4096u                             /* the reverb's pre-delay: up to 93 ms */

static int16_t cho_buf[CHO_N] ENGINE_POOL;
static int16_t dly_buf[2][DLY_N] ENGINE_POOL;
static int16_t pre_buf[PRE_N] ENGINE_POOL;

static inline int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x); }

/* a power-of-two line read d_q16 / 65536 samples back from w (the newest at w - 1), linearly interpolated */
static inline int32_t rd_frac(const int16_t *b, uint32_t mask, uint32_t w, uint32_t d_q16)
{
    uint32_t i = (w - 1u - (d_q16 >> 16)) & mask, f = d_q16 & 0xFFFFu;
    int32_t a = b[i], c = b[(i - 1u) & mask];
    return a + (int32_t)(((int64_t)(c - a) * f) >> 16);
}

/* ---- a pitch shifter: two read heads half a window apart, sweeping the window at (1 - ratio) a sample, each
 * faded by a triangle (they sum to one), so the pitch moves by ratio with no gaps */
typedef struct { int16_t b[SH_N]; uint32_t w; int32_t d; } shifter_t;   /* d: the first head's delay, Q16 */
static shifter_t sh_dly[2] ENGINE_POOL, sh_rev ENGINE_POOL;

static int32_t shift(shifter_t *s, int32_t x, uint32_t ratio_q16)
{
    const int32_t W = SH_W << 16;
    int32_t d1, d2, g1, g2;
    s->b[s->w & (SH_N - 1u)] = sat16(x);
    s->w++;
    s->d += 65536 - (int32_t)ratio_q16;         /* up (ratio > 1): the heads catch up with the writes */
    if (s->d < 0) s->d += W;
    if (s->d >= W) s->d -= W;
    d1 = s->d;
    d2 = d1 + W / 2;
    if (d2 >= W) d2 -= W;
    g1 = d1 < W / 2 ? (int32_t)((int64_t)d1 * 65536 / W) : (int32_t)((int64_t)(W - d1) * 65536 / W);   /* Q15 */
    g2 = 32768 - g1;
    return (int32_t)(((int64_t)rd_frac(s->b, SH_N - 1u, s->w, (uint32_t)d1 + (64u << 16)) * g1 +
                      (int64_t)rd_frac(s->b, SH_N - 1u, s->w, (uint32_t)d2 + (64u << 16)) * g2) >> 15);
}

/* ---- the plate's lines (any length): read d samples back (1 = the newest), write, an allpass */
typedef struct { int16_t *b; uint16_t n, w; } line_t;
static inline int32_t ln_rd(const line_t *l, uint32_t d)
{
    int32_t i = (int32_t)l->w - (int32_t)d;
    return l->b[i < 0 ? i + l->n : i];
}
static inline int32_t ln_rd_frac(const line_t *l, uint32_t d_q16)   /* d_q16 / 65536 back, interpolated */
{
    uint32_t d = d_q16 >> 16, f = d_q16 & 0xFFFFu;
    int32_t a = ln_rd(l, d), c = ln_rd(l, d + 1u);
    return a + (int32_t)(((int64_t)(c - a) * f) >> 16);
}
static inline void ln_wr(line_t *l, int32_t v)
{
    l->b[l->w] = sat16(v);
    if (++l->w == l->n) l->w = 0;
}
static inline int32_t allpass(line_t *l, int32_t x, int32_t g, uint32_t d_q16)   /* g Q15; the delay d_q16 */
{
    int32_t z = d_q16 ? ln_rd_frac(l, d_q16) : ln_rd(l, l->n), v = x - ((z * g) >> 15);
    ln_wr(l, v);
    return z + ((v * g) >> 15);
}

/* Dattorro's lengths at 44.1 kHz (x 1.4818); the tank's first allpasses have room for the sweep (EXC samples) */
#define EXC 40u
enum { L_IN1 = 210, L_IN2 = 159, L_IN3 = 562, L_IN4 = 410,
       L_AP1L = 996, L_D1L = 6598, L_AP2L = 2667, L_D2L = 5512,
       L_AP1R = 1345, L_D1R = 6249, L_AP2R = 3936, L_D2R = 4687 };
static int16_t rv_mem[L_IN1 + L_IN2 + L_IN3 + L_IN4 + L_AP1L + EXC + 2 + L_D1L + L_AP2L + L_D2L +
                      L_AP1R + EXC + 2 + L_D1R + L_AP2R + L_D2R] ENGINE_POOL;
static line_t rv_in[4], rv_ap1[2], rv_d1[2], rv_ap2[2], rv_d2[2];
static int32_t rv_lp[2];                        /* the tank's damping low-passes */

/* ---- derived by fx_set() (main loop), read by fx_render() (the ISR) */
static volatile struct {
    int32_t cho_mix, cho_depth;                 /* Q15; the sweep's depth in samples, Q16 */
    uint32_t cho_inc;                           /* the sweep's phase step a sample */
    int32_t dly_mix, dly_fb, dly_tone;          /* Q15 */
    uint32_t dly_time;                          /* samples, Q8 (glided to by the ISR) */
    uint32_t dly_ping, dly_ratio;               /* ratio Q16 (65536: no shift) */
    uint32_t wow_inc;
    int32_t wow_depth;                          /* samples, Q16 */
    int32_t rev_mix, rev_decay, rev_damp, rev_shim;   /* Q15 */
    uint32_t rev_pre;                           /* samples */
    uint32_t rev_inc;
    int32_t rev_depth;                          /* the tank allpasses' sweep, samples Q16 */
} fxp;

static uint32_t cho_w, cho_ph, dly_w, wow_ph, pre_w, rev_ph, dly_time_q8;
static int32_t dly_lp[2];

static void fx_init(void)                       /* the plate's lines over rv_mem */
{
    static const uint16_t LEN[12] = {L_IN1, L_IN2, L_IN3, L_IN4, L_AP1L + EXC + 2, L_D1L, L_AP2L, L_D2L,
                                     L_AP1R + EXC + 2, L_D1R, L_AP2R, L_D2R};
    line_t *all[12] = {&rv_in[0], &rv_in[1], &rv_in[2], &rv_in[3], &rv_ap1[0], &rv_d1[0], &rv_ap2[0], &rv_d2[0],
                       &rv_ap1[1], &rv_d1[1], &rv_ap2[1], &rv_d2[1]};
    uint32_t k, at = 0;
    for (k = 0; k < 12u; k++) {
        all[k]->b = rv_mem + at;
        all[k]->n = LEN[k];
        all[k]->w = 0;
        at += LEN[k];
    }
}

static void fx_set(const patch_t *p)
{
    uint32_t r;
    fxp.cho_mix = PR_CURVE_Q15[p->cho_mix & 127u] / 2;   /* full = half wet: the classic chorus */
    fxp.cho_depth = (int32_t)(p->cho_depth & 127u) * (260 << 16) / 127;   /* up to +-6 ms */
    fxp.cho_inc = PR_LFO_INC[p->cho_rate & 127u] >> BLK_SHIFT;
    fxp.dly_mix = PR_CURVE_Q15[p->dly_mix & 127u];
    fxp.dly_fb = (int32_t)(p->dly_fb & 127u) * 31000 / 127;   /* up to 0.95 */
    fxp.dly_tone = 3000 + (int32_t)(p->dly_tone & 127u) * 29000 / 127;
    fxp.dly_time = (441u + (uint32_t)(p->dly_time & 127u) * (DLY_N - 1100u) / 127u) << 8;   /* 10 .. 735 ms */
    if (!dly_time_q8)
        dly_time_q8 = fxp.dly_time;             /* (the first time: no glide up from nothing) */
    fxp.dly_ping = p->dly_ping ? 1u : 0u;
    r = p->dly_shift <= 24u ? p->dly_shift : 12u;
    fxp.dly_ratio = PR_SEMI_Q16[12u + r];       /* -12 .. +12 semitones a repeat */
    fxp.wow_inc = PR_LFO_INC[p->dly_wrate & 127u] >> BLK_SHIFT;
    fxp.wow_depth = (int32_t)(p->dly_wdepth & 127u) * (130 << 16) / 127;   /* up to +-3 ms */
    fxp.rev_mix = PR_CURVE_Q15[p->rev_mix & 127u];
    fxp.rev_decay = 6000 + (int32_t)(p->rev_size & 127u) * 25500 / 127;   /* 0.18 .. 0.96 */
    fxp.rev_damp = 32767 - (int32_t)(p->rev_damp & 127u) * 29000 / 127;
    fxp.rev_shim = (int32_t)(p->rev_shimmer & 127u) * 26000 / 127;   /* the share shifted: up to 0.8 */
    fxp.rev_pre = 1u + (uint32_t)(p->rev_pre & 127u) * (PRE_N - 2u) / 127u;
    fxp.rev_inc = PR_LFO_INC[p->rev_mrate & 127u] >> BLK_SHIFT;
    fxp.rev_depth = (int32_t)(p->rev_mdepth & 127u) * ((int32_t)EXC << 16) / 127;
}

/* ---- the audio ISR: out (L R, the voices) -> chorus, delay, reverb, in place */
static void fx_render(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t cmix = fxp.cho_mix, dmix = fxp.dly_mix, rmix = fxp.rev_mix;
    if (!cmix && !dmix && !rmix)
        return;
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u];
        if (cmix) {                             /* chorus: the mono sum into the line; two swept taps */
            int32_t sl = lfo_wave(LFO_SINE, cho_ph, 0), sr = lfo_wave(LFO_SINE, cho_ph + 0x40000000u, 0), wl, wr;
            uint32_t base = 309u << 16;         /* 7 ms */
            cho_buf[cho_w & (CHO_N - 1u)] = sat16((l + r) >> 1);
            cho_w++;
            cho_ph += fxp.cho_inc;
            wl = rd_frac(cho_buf, CHO_N - 1u, cho_w, base + (uint32_t)(((int64_t)fxp.cho_depth * sl) >> 15));
            wr = rd_frac(cho_buf, CHO_N - 1u, cho_w, base + (uint32_t)(((int64_t)fxp.cho_depth * sr) >> 15));
            l += ((wl - l) * cmix) >> 15;
            r += ((wr - r) * cmix) >> 15;
        }
        if (dmix) {                             /* delay: wow on its time, feedback through tone (and shift) */
            int32_t wob = (int32_t)(((int64_t)fxp.wow_depth * lfo_wave(LFO_SINE, wow_ph, 0)) >> 15), tl, tr, fl, fr;
            uint32_t d;
            if ((i & (BLK - 1u)) == 0u) {       /* the time glides (a change bends the repeats, as tape does) */
                int32_t dd = (int32_t)(fxp.dly_time - dly_time_q8) >> 6;
                dly_time_q8 = dd ? dly_time_q8 + (uint32_t)dd : fxp.dly_time;
            }
            wow_ph += fxp.wow_inc;
            d = (dly_time_q8 << 8) + (uint32_t)wob;
            tl = rd_frac(dly_buf[0], DLY_N - 1u, dly_w, d);
            tr = rd_frac(dly_buf[1], DLY_N - 1u, dly_w, d);
            dly_lp[0] += ((tl - dly_lp[0]) * fxp.dly_tone) >> 15;   /* the repeats darken */
            dly_lp[1] += ((tr - dly_lp[1]) * fxp.dly_tone) >> 15;
            fl = (dly_lp[0] * fxp.dly_fb) >> 15;
            fr = (dly_lp[1] * fxp.dly_fb) >> 15;
            if (fxp.dly_ratio != 65536u) {      /* SHIFT: each repeat a step up or down */
                fl = shift(&sh_dly[0], fl, fxp.dly_ratio);
                fr = shift(&sh_dly[1], fr, fxp.dly_ratio);
            }
            if (fxp.dly_ping) {                 /* ping-pong: in on the left, then across and back */
                dly_buf[0][dly_w & (DLY_N - 1u)] = sat16(((l + r) >> 1) + fr);
                dly_buf[1][dly_w & (DLY_N - 1u)] = sat16(fl);
            } else {
                dly_buf[0][dly_w & (DLY_N - 1u)] = sat16(l + fl);
                dly_buf[1][dly_w & (DLY_N - 1u)] = sat16(r + fr);
            }
            dly_w++;
            l += (tl * dmix) >> 15;
            r += (tr * dmix) >> 15;
        }
        if (rmix) {                             /* the plate */
            int32_t x, sl, sr, yl, yr, a, b2;
            uint32_t e0, e1;
            x = (l + r) >> 2;                   /* (half the mono sum: headroom in the tank) */
            pre_buf[pre_w & (PRE_N - 1u)] = sat16(x);
            pre_w++;
            x = pre_buf[(pre_w - 1u - fxp.rev_pre) & (PRE_N - 1u)];
            x = allpass(&rv_in[0], x, 24576, 0);   /* 0.75, 0.75, 0.625, 0.625 */
            x = allpass(&rv_in[1], x, 24576, 0);
            x = allpass(&rv_in[2], x, 20480, 0);
            x = allpass(&rv_in[3], x, 20480, 0);
            rev_ph += fxp.rev_inc;              /* the tank's sweep: left and right a quarter cycle apart */
            e0 = (uint32_t)(((int32_t)(L_AP1L + EXC / 2u) << 16) +
                            (int32_t)(((int64_t)fxp.rev_depth * lfo_wave(LFO_SINE, rev_ph, 0)) >> 16));
            e1 = (uint32_t)(((int32_t)(L_AP1R + EXC / 2u) << 16) +
                            (int32_t)(((int64_t)fxp.rev_depth * lfo_wave(LFO_SINE, rev_ph + 0x40000000u, 0)) >> 16));
            a = ln_rd(&rv_d2[0], L_D2L);        /* the two sides' ends */
            b2 = ln_rd(&rv_d2[1], L_D2R);
            if (fxp.rev_shim)                   /* shimmer: the left end, partly an octave up, feeds the right */
                a += ((shift(&sh_rev, a, 131072u) - a) * fxp.rev_shim) >> 15;
            sl = x + ((b2 * fxp.rev_decay) >> 15);   /* each side fed by the other's end */
            sr = x + ((a * fxp.rev_decay) >> 15);
            sl = allpass(&rv_ap1[0], sl, -22938, e0);   /* -0.70: decay diffusion 1, swept */
            sr = allpass(&rv_ap1[1], sr, -22938, e1);
            a = ln_rd(&rv_d1[0], L_D1L);
            b2 = ln_rd(&rv_d1[1], L_D1R);
            ln_wr(&rv_d1[0], sl);
            ln_wr(&rv_d1[1], sr);
            rv_lp[0] += ((a - rv_lp[0]) * fxp.rev_damp) >> 15;   /* damping */
            rv_lp[1] += ((b2 - rv_lp[1]) * fxp.rev_damp) >> 15;
            a = allpass(&rv_ap2[0], (rv_lp[0] * fxp.rev_decay) >> 15, 16384, 0);   /* 0.5: decay diffusion 2 */
            b2 = allpass(&rv_ap2[1], (rv_lp[1] * fxp.rev_decay) >> 15, 16384, 0);
            ln_wr(&rv_d2[0], a);
            ln_wr(&rv_d2[1], b2);
            /* the outputs: Dattorro's taps, scaled */
            yl = ln_rd(&rv_d1[1], 394) + ln_rd(&rv_d1[1], 4407) - ln_rd(&rv_ap2[1], 2835) + ln_rd(&rv_d2[1], 2958) -
                 ln_rd(&rv_d1[0], 2949) - ln_rd(&rv_ap2[0], 277) - ln_rd(&rv_d2[0], 1580);
            yr = ln_rd(&rv_d1[0], 523) + ln_rd(&rv_d1[0], 5374) - ln_rd(&rv_ap2[0], 1820) + ln_rd(&rv_d2[0], 3961) -
                 ln_rd(&rv_d1[1], 3128) - ln_rd(&rv_ap2[1], 496) - ln_rd(&rv_d2[1], 179);
            l += (((yl * 19661) >> 15) * rmix) >> 15;   /* (0.6: the seven taps add up) */
            r += (((yr * 19661) >> 15) * rmix) >> 15;
        }
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
