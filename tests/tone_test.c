/* SPDX-License-Identifier: GPL-3.0-only */
/* The tone library (tones.c), on a computer, through the app's own code: every factory tone's changes travel in a tone
 * (none dropped); the names fit and differ; a tone into part B and back is the same tone, its destinations its own
 * oscillator's; a tone sounds the same in part A and in part B; each factory tone sounds; a user tone stored is there
 * after power-on, the rest empty; the PATCH screen's TONE row loads into the part edited (UNDO brings the sound back),
 * stores the part edited under a name, asks before replacing, and keeps the factory's fixed.
 *   tests/run_engine.sh runs it */
#include <unistd.h>
#include <sys/wait.h>
#define main prysm_host_main
#include "prysm.c"
#undef main

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf("  %s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static int32_t buf[2u * BLK];
static void start(const patch_t *p)
{
    uint32_t v, n;
    engine_all_off();
    for (v = 0; v < NV; v++) voice[v].t_g[0] = voice[v].t_g[1] = 0;
    for (n = 0; n < 44100u * 2u; n += BLK) { memset(buf, 0, sizeof buf); engine_render(buf, BLK); }
    edit = *p;
    engine_init();
    engine_set(&edit);
}
static uint64_t heard(const patch_t *p, uint32_t part, uint64_t *energy)   /* a note on that part (DUAL), 1.5 s */
{
    uint64_t h = 1469598103934665603ull, e = 0;
    uint32_t n, i;
    start(p);
    key_part = (uint8_t)part;
    engine_note_on_vel(60, 100);
    for (n = 0; n < 66150u; n += BLK) {
        memset(buf, 0, sizeof buf);
        engine_render(buf, BLK);
        for (i = 0; i < 2u * BLK; i++) {
            h ^= (uint32_t)buf[i]; h *= 1099511628211ull;
            e += (uint64_t)((int64_t)buf[i] * buf[i] >> 16);
        }
    }
    key_part = 0;
    if (energy) *energy = e;
    return h;
}
static patch_t dual(void)                       /* DUAL, no effects: a part alone */
{
    patch_t p = PATCH_INIT;
    p.vmode = VM_DUAL;
    p.cho_mix = p.dly_mix = p.rev_mix = 0;
    return p;
}

int main(void)
{
    static patch_t p, q;
    static tone_t t, t2;
    uint32_t i, j, ok, quiet = 0, same = 0;
    uint64_t ea, eb;
    printf("TONES\n");

    for (i = 0, ok = 1; i < TONE_ROM_N; i++) {  /* each its changes, all kept */
        const uint8_t *kv = TONE_ROM[i].kv;
        p = PATCH_INIT;
        for (; kv[0] != TE; kv += 2) ((uint8_t *)&p)[kv[0]] = kv[1];
        tone_rom(i, &t);
        q = PATCH_INIT;
        tone_put(&q, 0, &t);
        for (j = 0; j < PART_N; j++) q.pb[j] = p.pb[j];   /* (part B: PATCH_INIT's, as p's) */
        if (memcmp(&p, &q, sizeof p)) {
            for (j = 0; j < sizeof p; j++)
                if (((uint8_t *)&p)[j] != ((uint8_t *)&q)[j]) printf("    %s: offset %u not in a tone\n", TONE_ROM[i].name, j);
            ok = 0;
        }
    }
    CHECK(ok, "every factory tone's changes travel in a tone");
    for (i = 0, ok = 1; i < TONE_ROM_N; i++) {
        ok &= strlen(TONE_ROM[i].name) > 0 && strlen(TONE_ROM[i].name) < TONE_NAME;
        for (j = i + 1u; j < TONE_ROM_N; j++)
            if (!strcmp(TONE_ROM[i].name, TONE_ROM[j].name)) { printf("    %s twice\n", TONE_ROM[i].name); ok = 0; }
    }
    CHECK(ok, "the factory tones' names fit (9 characters) and differ");

    /* into part B and back: the same tone; a destination of its own osc (WAVE1) stays WAVE1 (engine.c: osc 2's) */
    for (i = 0, ok = 1; i < TONE_ROM_N; i++) {
        tone_rom(i, &t);
        p = PATCH_INIT;
        tone_put(&p, 1, &t);
        tone_get(&p, 1, &t2);
        memcpy(t2.name, t.name, TONE_NAME);
        if (memcmp(&t, &t2, sizeof t)) { printf("    %s: not the same from B\n", t.name); ok = 0; }
    }
    CHECK(ok, "every factory tone into part B and back is the same tone");
    p = PATCH_INIT;
    p.lfo_dest[1] = DEST_WAVE2;                 /* (part A pointing at osc 2: kept as it is) */
    tone_get(&p, 0, &t);
    CHECK(t.pf[18] == DEST_WAVE2, "part A's destinations are kept as they are");
    for (j = 0; j < PART_N; j++)
        if (PART_OFF[j] == __builtin_offsetof(patch_t, lfo_dest[1])) p.pb[j] = DEST_WAVE2;
    tone_get(&p, 1, &t);
    CHECK(t.pf[18] == DEST_WAVE1, "part B's osc-2 destinations become the tone's own (WAVE1)");

    /* heard: each factory tone sounds, in A as in B */
    for (i = 0; i < TONE_ROM_N; i++) {
        uint64_t ha, hb;
        tone_rom(i, &t);
        t.pf[16] = 1;                           /* (the LFO retriggered: A's free one and B's run apart) */
        p = dual();
        tone_put(&p, 0, &t);
        ha = heard(&p, 0, &ea);
        p = dual();
        tone_put(&p, 1, &t);
        hb = heard(&p, 1, &eb);
        if (ea < 1000u) { printf("    %s: quiet (%llu)\n", t.name, (unsigned long long)ea); quiet++; }
        if (ha == hb || t.pf[13] == LFO_SH || (ea <= eb + eb / 10u && eb <= ea + ea / 10u)) same++;   /* (S&H: random) */
        else printf("    %s: A %llu, B %llu\n", t.name, (unsigned long long)ea, (unsigned long long)eb);
    }
    CHECK(!quiet, "every factory tone sounds");
    CHECK(same == TONE_ROM_N, "every factory tone as loud in part B as in part A, within 10 %% (%u of %u)", same, TONE_ROM_N);

    /* the user tones: stored, then there after power-on (in a half of its own); the rest empty */
    tone_user_h = 0xFFu;
    tone_rom(8, &t);
    CHECK(tone_user_get(0, &t2) != 0 && tone_at(TONE_ROM_N + 40u, &t2) != 0, "no user tones at first");
    memcpy(t.name, "MINE", 5);
    CHECK(tone_user_put(40, &t) == 0, "a user tone stored (40: the second half)");
    tone_user_h = 0xFFu;                        /* (power-on) */
    memset(&tone_user, 0, sizeof tone_user);
    CHECK(tone_at(TONE_ROM_N + 40u, &t2) == 0 && !memcmp(&t, &t2, sizeof t) && tone_user_get(39, &t2) != 0 &&
          tone_user_get(8, &t2) != 0, "after power-on: there, the others empty");

    /* the PATCH screen's TONE row */
    p = dual();
    start(&p);
    clean = edit;
    page = PG_PATCH;
    pa_row = PA_TONE;
    ui_part = 1;
    ui_view();
    pa_tact = TA_LOAD;
    pa_tone = 32;                               /* (DX BELL) */
    tone_rom(32, &t);
    CHECK(pa_do() == 1, "TONE: SAVE loads");
    tone_get(&edit, 1, &t2);
    memcpy(t2.name, t.name, TONE_NAME);
    CHECK(!memcmp(&t, &t2, sizeof t) && edit.lvl1 == PATCH_INIT.lvl1 && edit.osc[0].level == PATCH_INIT.osc[0].level &&
          !memcmp(&edit.osc[0], &PATCH_INIT.osc[0], sizeof edit.osc[0]), "into part B, the part edited; part A as it was");
    CHECK(!strcmp(pa_msg, "DX BELL INTO B"), "it says so (%s)", pa_msg);
    pa_row = PA_UNDO;
    pa_do();
    CHECK(!memcmp(&edit, &p, sizeof p), "UNDO: the sound before");
    pa_row = PA_TONE;
    ui_part = 0;
    edit = PATCH_INIT;                          /* (one part: osc 2 quiet, the tone as made) */
    ui_view();
    pa_do();
    CHECK(edit.lvl2 == 0 && edit.osc[0].ratio[1] == t.osc.ratio[1], "SINGLE: into A, osc 2's level down");
    edit.cut = 17;
    pa_tact = TA_STORE;
    pa_tone = 5;
    pa_armed = 0;
    CHECK(pa_do() == 0 && !strcmp(pa_msg, "FACTORY TONES ARE FIXED"), "STORE on a factory tone: no");
    pa_tone = TONE_ROM_N + 40u;                 /* (used: MINE) */
    pa_do();
    CHECK(pa_armed && tone_user_get(40, &t2) == 0 && !strcmp(t2.name, "MINE"), "STORE over a used tone asks first");
    pa_do();
    CHECK(tone_user_get(40, &t2) == 0 && !strcmp(t2.name, "MINE") && t2.pf[0] == 17, "again: stored, its name kept");
    pa_tname_turn(2, 4);                        /* (the fifth letter: B) */
    pa_tname_turn(3, 2);
    pa_do(); pa_do();
    CHECK(tone_user_get(40, &t2) == 0 && !strcmp(t2.name, "MINEB") && !strcmp(pa_msg, "STORED: MINEB"),
          "named: KNOB3 the place, KNOB4 the letter (%s)", t2.name);
    pa_tone = TONE_ROM_N + 7u;                  /* (an empty one: USER 8) */
    pa_tname_init();
    CHECK(!memcmp(pa_tname, "USER 8   ", 9), "an empty one's name: USER n");
    pa_tname_turn(3, -1);                       /* ('U' back to 'T') */
    pa_do();
    CHECK(tone_user_get(7, &t2) == 0 && !strcmp(t2.name, "TSER 8"), "stored with its name, the spaces after off");
    tone_user_h = 0xFFu;                        /* (power-on) */
    memset(&tone_user, 0, sizeof tone_user);
    CHECK(tone_user_get(40, &t2) == 0 && t2.pf[0] == 17, "and kept");
    tone_rom(1, &t);
    CHECK(tone_user_put(3, &t) == 0 && tone_user_get(40, &t2) == 0 && t2.pf[0] == 17 && tone_user_get(3, &t2) == 0 &&
          !strcmp(t2.name, "FM BASS"), "a tone stored in the first half: the second's as they were");

    printf(fails ? "TONE TESTS FAILED (%d)\n" : "TONE TESTS PASSED\n", fails);
    return fails != 0;
}
