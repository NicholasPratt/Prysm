/* SPDX-License-Identifier: GPL-3.0-only */
/* RANDOM (random.c), on a computer, through the app's own code: rolls from the default sound and from rolled ones, at
 * every AMOUNT and SECTION: every field in its knob's range (a choice a valid one, a destination one its knob offers);
 * a SECTION changes only its own fields; the arp, glide, velocity, split, VCA, routing never; a nudge (5 %) seldom
 * switches a choice; a note played on any NEW sound is heard (none 36 dB under the default sound, most within 12 dB)
 * and is never stuck (it ends after the key is let go);
 * the same seed rolls the same sound.
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t buf[2u * BLK];
static uint64_t run(uint32_t n)                 /* n frames; their energy */
{
    uint64_t e = 0;
    uint32_t i;
    for (; n >= BLK; n -= BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
        for (i = 0; i < 2u * BLK; i++) e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
    }
    return e;
}

/* the loudest 512 frames of a note held 0.5 s (the voices alone: no effects, their tails not the note's); *tail:
 * what is left 12 s after the key is let go (the longest release is 10 s) */
static uint64_t peak(patch_t p, uint64_t *tail)
{
    uint64_t best = 0, e;
    uint32_t n;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    p.mfx_type = MFX_OFF;
    edit = p;
    engine_init();
    engine_set(&edit);
    engine_note_on_vel(48, 127);
    for (n = 0; n < 24576u; n += 512u)
        if ((e = run(512)) > best) best = e;
    engine_note_off(48);
    run(48000 * 12);
    *tail = run(4800);
    return best;
}

static int valid(const patch_t *p)              /* every field as a knob could set it */
{
    uint32_t o, m;
    for (o = 0; o < NOSC; o++) {
        for (m = 0; m < NOP; m++)
            if (p->osc[o].bank[m] >= PW_BANKS || p->osc[o].pos[m] > 127u || p->osc[o].ratio[m] >= PR_RATIOS) return 0;
        for (m = 0; m < 2u; m++)
            if (p->osc[o].mode[m] >= MODE_N || p->mpitch[o][m] > 48u || p->mfine[o][m] > 127u) return 0;
    }
    if (p->semi2 > 48u || p->flt_type >= FLT_N || p->noise_type >= NOISE_N || p->lfo_wave >= LFO_N ||
        p->lfo2_wave >= LFO_N || p->lfo2_sync >= L2SYNC_N || p->mfx_type >= MFX_N || p->dly_shift > 24u ||
        p->menv_loop > 1u || p->menv2_loop > 1u || p->dly_ping > 1u || p->dly_sync > 1u || p->lfo_retrig > 1u ||
        p->trn_type[0] > TR_N || p->trn_type[1] > TR_N || p->trn_tune[0] > 48u || p->trn_tune[1] > 48u)
        return 0;
    if (!DEST_LFO_OK(p->lfo_dest[0]) || !DEST_LFO_OK(p->lfo_dest[1]) || !DEST_LFO2_OK(p->lfo2_dest) ||
        p->menv_dest >= DEST_N || p->menv2_dest >= DEST_N)
        return 0;
    for (m = 0; m < MM_SLOTS; m++)
        if (p->mm_src[m] >= MM_N || !(p->mm_dest[m] == DEST_OFF || DEST_LFO_OK(p->mm_dest[m]))) return 0;
    for (o = 0; o < sizeof *p; o++)             /* (every field is 0..127 but arp_bpm) */
        if (((const uint8_t *)p)[o] > 127u && o != __builtin_offsetof(patch_t, arp_bpm)) return 0;
    return 1;
}

/* the fields a SECTION may change, as byte ranges [a, b) of patch_t */
#define F(x) __builtin_offsetof(patch_t, x)
#define FE(x) (__builtin_offsetof(patch_t, x) + sizeof ((patch_t *)0)->x)
static int in_sec(uint32_t off, uint32_t sec)
{
    switch (sec) {
    case RS_OSC: return off < FE(osc) || (off >= F(lvl1) && off < FE(fine2)) || (off >= F(mpitch) && off < FE(mfine)) ||
                        (off >= F(noise_type) && off < FE(noise_lvl)) || (off >= F(trn_type) && off < FE(trn_dec));
    case RS_FILTER: return (off >= F(cut) && off < FE(drive)) || (off >= F(hp_cut) && off < FE(comb_spread)) ||
                           off == F(hp_res);
    case RS_ENV: return (off >= F(flt_a) && off < FE(amp_r)) || (off >= F(menv_a) && off < FE(menv_loop)) ||
                        (off >= F(menv2_a) && off < FE(menv2_loop));
    case RS_MOD: return (off >= F(lfo_wave) && off < FE(lfo_amt)) || (off >= F(menv_dest) && off < FE(menv_amt)) ||
                        (off >= F(lfo2_wave) && off < FE(lfo2_amt)) || (off >= F(menv2_dest) && off < FE(menv2_amt)) ||
                        (off >= F(mm_src) && off < FE(mm_amt));
    case RS_FX: return (off >= F(cho_rate) && off < FE(rev_shimmer)) || (off >= F(mfx_type) && off < FE(crush_rate)) ||
                       off == F(dly_sync);
    }
    return 1;
}
static int never(uint32_t off)                  /* RANDOM leaves these as they were */
{
    return (off >= F(vel_amp) && off < FE(arp_swing)) || off == F(vca) || (off >= F(route) && off < FE(split_oct)) ||
           (off >= F(mm_time) && off < FE(mm_time));
}

int main(void)
{
    uint32_t seed, sec, pct, i, bad = 0, out = 0, never_bad = 0, quiet = 0, loud = 0, stuck = 0, sw = 0, swn = 0;
    uint64_t ref, tail;
    patch_t p, q, r;
    printf("RANDOM\n");
    for (sec = 0; sec < RS_N; sec++)            /* from the default, and from a sound already rolled, chained */
        for (pct = 5; pct <= 100u; pct += 5) {
            p = PATCH_INIT;
            for (seed = 1; seed <= 40u; seed++) {
                q = p;
                patch_random(&p, pct, sec, seed * 7919u + pct);
                bad += !valid(&p);
                for (i = 0; i < sizeof p; i++)
                    if (((uint8_t *)&p)[i] != ((uint8_t *)&q)[i]) {
                        if (never(i)) never_bad++;
                        else if (sec != RS_ALL && !in_sec(i, sec) && i != F(lvl1) && i != F(amp_d) && i != F(cut) &&
                                 i != F(hp_cut) && i != F(lfo_amt) && i != F(lfo_amt) + 1u)
                            out++;          /* (the audible guards may touch those few, whatever the section) */
                    }
            }
        }
    CHECK(bad == 0, "every field in its knob's range (%u bad of %u rolls)", bad, RS_N * 20u * 40u);
    CHECK(out == 0, "a SECTION changes only its own fields (%u outside)", out);
    CHECK(never_bad == 0, "arp, glide, velocity, split, VCA, routing, drum TIME untouched (%u changed)", never_bad);
    for (seed = 1; seed <= 300u; seed++) {      /* a nudge: choices seldom switch */
        p = PATCH_INIT;
        q = p;
        patch_random(&p, 5, RS_ALL, seed);
        sw += p.flt_type != q.flt_type; sw += p.lfo_wave != q.lfo_wave; sw += p.osc[0].bank[0] != q.osc[0].bank[0];
        sw += p.mfx_type != q.mfx_type; sw += p.semi2 != q.semi2; sw += (p.dly_mix != 0) != (q.dly_mix != 0);
        swn += 6;
    }
    CHECK(sw * 100u < swn * 2u, "a 5%% nudge switches a choice seldom (%u of %u)", sw, swn);
    p = PATCH_INIT; q = PATCH_INIT;
    patch_random(&p, 100, RS_ALL, 1234);
    patch_random(&q, 100, RS_ALL, 1234);
    r = PATCH_INIT;
    patch_random(&r, 100, RS_ALL, 1235);
    CHECK(!memcmp(&p, &q, sizeof p) && memcmp(&p, &r, sizeof p), "the same seed rolls the same sound, another another");
    ref = peak(PATCH_INIT, &tail);              /* the default sound's loudness: what NEW ones are held to */
    for (seed = 1; seed <= 1000u; seed++) {     /* NEW sounds: a note heard, and gone after the key is let go */
        uint64_t e;
        p = PATCH_INIT;
        patch_random(&p, 100, RS_ALL, seed * 104729u);
        e = peak(p, &tail);
        if (e * 4000u < ref) { quiet++; printf("    quiet: seed %u\n", seed * 104729u); }   /* (-36 dB) */
        if (e * 16u >= ref) loud++;             /* (within 12 dB) */
        if (tail > 50u) stuck++;
    }
    CHECK(quiet == 0, "every NEW sound is heard: none 36 dB under the default (%u of 1000)", quiet);
    CHECK(loud >= 850u, "most within 12 dB of it (%u of 1000)", loud);
    CHECK(stuck == 0, "and each ends after the key is let go (%u of 1000 still sounding)", stuck);
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
