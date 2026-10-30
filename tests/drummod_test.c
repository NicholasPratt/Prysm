/* SPDX-License-Identifier: GPL-3.0-only */
/* The drums as the mod matrix's sources and the delay in time, on a computer, through the app's own code: a kick on
 * AMP ducks a held note (down) or gates it (up), with its level at 0 too; from the sequencer and from MIDI; TIME the
 * duck's length; a LIVE mute: no duck; another drum's hits: none. SYNC: TIME a division of the tempo, following it,
 * halved past the line's length.
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t buf[2u * BLK];

static uint64_t run(uint32_t n, uint32_t from)  /* n frames on; the energy of those from frame `from` on */
{
    uint64_t e = 0;
    uint32_t i, at = 0;
    for (; n >= BLK; n -= BLK, at += BLK) {
        memset(buf, 0, sizeof buf);             /* (the render adds into it) */
        engine_render(buf, BLK);
        if (at >= from)
            for (i = 0; i < 2u * BLK; i++) e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
    }
    return e;
}

/* a held note (the amp held open), a route src -> AMP by amt, TIME t; the kick's level 0 (silent): the note's energy
 * in the 2000 frames after the kick (how: 0 the sequencer's step 1, 1 a MIDI note), and from 12000 .. 14000 */
static uint64_t late;
static uint32_t mute_kick;
static uint64_t duck_in(uint32_t src, uint32_t amt, uint32_t t, uint32_t how)
{
    uint64_t e;
    engine_seq_play(0);
    run(BLK, 0);
    seqs_init();
    live_mute = 0;
    edit = PATCH_INIT;
    edit.arp_bpm = 80;
    edit.amp_s = 127; edit.amp_a = 0;
    edit.cho_mix = edit.dly_mix = edit.rev_mix = 0;   /* (the voices alone: the effects after the amp are not ducked) */
    edit.mm_src[0] = (uint8_t)src; edit.mm_dest[0] = DEST_AMP; edit.mm_amt[0] = (uint8_t)amt; edit.mm_time[0] = (uint8_t)t;
    engine_init();
    engine_set(&edit);
    engine_sync(0);
    seqs.dr_lvl[0] = 0;                         /* (the kick silent: its hits still count) */
    engine_note_on_vel(48, 127);
    run(8000, 0);                               /* (the note settled) */
    if (mute_kick) live_mute = 1u << LM_KICK;
    if (how == 0) {
        drum_put(&seqs.pat[0], 0, 0, 1);
        engine_seq_play(1);
    } else
        engine_drum(DR_KICK_S, 100);
    e = run(2000, 0);
    run(10000, 0);
    late = run(2000, 0);
    engine_note_off(48);
    return e;
}

/* duck_in in a process of its own: from power-on each time (engine_init leaves the voices, the effects as they are);
 * how 2: as 1, with the kick muted on LIVE */
static uint64_t duck(uint32_t src, uint32_t amt, uint32_t t, uint32_t how)
{
    int fd[2];
    uint64_t r[2] = {~0ull, ~0ull};
    pid_t c;
    if (pipe(fd)) return ~0ull;
    fflush(stdout);
    if (!(c = fork())) {
        if (how == 2u) mute_kick = 1;
        r[0] = duck_in(src, amt, t, how ? 1u : 0u);
        r[1] = late;
        if (write(fd[1], r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    if (read(fd[0], r, sizeof r) != sizeof r) r[0] = ~0ull;
    waitpid(c, 0, 0);
    close(fd[0]); close(fd[1]);
    late = r[1];
    return r[0];
}

int main(void)
{
    uint64_t none, dk, gt, shrt, lng, midi, muted, snare, l0;
    uint32_t z, ok;
    engine_init();

    /* ---- the drums as sources */
    none = duck(MM_OFF, 0, 70, 0); l0 = late;
    CHECK(none > 0u, "a held note sounds (%llu)", (unsigned long long)none);
    dk = duck(MM_KICK, 0, 70, 0);
    CHECK(dk < none / 2u, "KICK -> AMP -64: the sequencer's kick (level 0) ducks the note (%llu vs %llu)",
          (unsigned long long)dk, (unsigned long long)none);
    CHECK(late > l0 * 9u / 10u, "... and it comes back (%llu vs %llu)", (unsigned long long)late, (unsigned long long)l0);
    gt = duck(MM_KICK, 127, 70, 0);
    CHECK(gt > none / 2u && late < l0 / 100u, "KICK -> AMP +63: a gate: open on the hit, closed after (%llu, %llu)",
          (unsigned long long)gt, (unsigned long long)late);
    shrt = duck(MM_KICK, 0, 30, 0);
    lng = duck(MM_KICK, 0, 110, 0);
    CHECK(shrt > dk && dk > lng, "TIME: a short duck lets more through (%llu > %llu > %llu)",
          (unsigned long long)shrt, (unsigned long long)dk, (unsigned long long)lng);
    midi = duck(MM_KICK, 0, 70, 1);
    CHECK(midi < none / 2u, "a kick from MIDI (channel 10) ducks it too (%llu)", (unsigned long long)midi);
    snare = duck(MM_SNARE, 0, 70, 0);
    CHECK(snare > none - none / 1000u && snare <= none, "SNARE's route: the kick's hits move nothing (%llu; AMP's x 32767/32768 aside)", (unsigned long long)snare);
    muted = duck(MM_KICK, 0, 70, 2);
    CHECK(muted > none - none / 1000u && muted <= none, "the kick muted on LIVE: no duck (%llu)", (unsigned long long)muted);

    /* ---- the delay in time: 120 BPM, a tick 918.75 frames */
    edit = PATCH_INIT;
    edit.arp_bpm = 80;
    engine_set(&edit);
    run(BLK, 0);
    CHECK(fx_tick_q8 == 235200u, "120 BPM: a tick of %u/256 frames", (unsigned)fx_tick_q8);
    for (z = 0, ok = 1; z < DSYNC_N; z++)
        ok &= DSYNC_OF((z * 128u + 64u) / DSYNC_N) == z;
    CHECK(ok, "each division's knob value (the middle of its share) reads back as it");
    CHECK(dly_synced(8u * 128u / DSYNC_N + 5u) >> 8 == 22050u, "1/4: 22050 frames (%u)", (unsigned)(dly_synced(8u * 128u / DSYNC_N + 5u) >> 8));
    CHECK(dly_synced(127) >> 8 == 22050u, "1/2 (44100): past the line, halved to 22050");
    CHECK(dly_synced(0) >> 8 == 2756u, "1/32: 2756 frames");
    edit.dly_sync = 1; edit.dly_time = 5u * 128u / DSYNC_N + 5u; edit.dly_mix = 100;   /* 1/8 */
    engine_set(&edit);
    run(44100, 0);
    CHECK(dly_time_q8 >> 8 == 11025u, "SYNC 1/8: the delay glides to 11025 frames (%u)", (unsigned)(dly_time_q8 >> 8));
    edit.arp_bpm = 140 - 40;                    /* 140 BPM */
    engine_set(&edit);
    run(44100, 0);
    CHECK(dly_time_q8 >> 8 == 9450u, "140 BPM: it follows, 9450 frames (%u)", (unsigned)(dly_time_q8 >> 8));
    edit.dly_sync = 0;
    engine_set(&edit);
    run(44100, 0);
    CHECK(dly_time_q8 >> 8 == dly_time_of(edit.dly_time) >> 8, "SYNC OFF: TIME in ms again (%u)", (unsigned)(dly_time_q8 >> 8));

    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
