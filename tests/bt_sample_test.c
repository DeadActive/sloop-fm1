/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The Bluetooth build's built-in SAMPLE sets left out (tools/gen_samples.py FELUCCA_SMP_DROP=BASS,VIBES,HORNS,FLUTE;
 * built against that header: tests/run_tests.sh) on SLOOP's UI harness with FELUCCA_BLE=1: a sound on a set left out
 * plays a sine at the note's pitch (eng_sample.c smp_sine) and the top bar says NO SAMPLE once (bt_page.c smp_notice);
 * a set that is in (PIANO) plays its samples; the drum track's GM kit (PERC) is in. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }
#include "bt_host.h"
#define ok(what, good) check((good), (what))
static uint32_t set_of(const char *name)
{
    uint32_t i;
    for (i = 0; i < SMP_NSETS; i++)
        if (str_eq(SMP_SETS[i].name, name))
            return i;
    return 0xFFu;
}
/* track 1 on SAMPLE, SET si, a note held: the peak of its output over 40 blocks, and the zero crossings (pitch) */
static int32_t play(uint32_t si, uint32_t *cross)
{
    static int32_t o[CTL * 2];
    int32_t pk = 0, last = 0;
    uint32_t b, i;
    track_t *t = &trk[0];
    set_engine_of(t, 4);                             /* SAMPLE (engines.c ENGINES[4]) */
    t->engine = t->eng_req;
    t->p[P_E0] = (int16_t)si;
    t->p[P_E1] = 0;
    frames(4);
    input_on(t, 69, 100);                            /* A4: 440 Hz */
    *cross = 0;
    for (b = 0; b < 40u; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t v = o[2u * i];
            pk = v > pk ? v : -v > pk ? -v : pk;
            if (b >= 8u && ((last < 0) != (v < 0)))
                (*cross)++;
            last = v;
        }
    }
    input_off(t, 69);
    frames(30);
    return pk;
}

int main(void)
{
    uint32_t bass = set_of("BASS"), piano = set_of("PIANO"), c;
    int32_t pk;
    bt_power_on();
    bt_ui_init();
    ok("sets: BASS VIBES HORNS FLUTE left out (no data), PIANO STRGS SCRCH PERC in",
       smp_set_missing(bass) && smp_set_missing(set_of("VIBES")) && smp_set_missing(set_of("HORNS")) &&
       smp_set_missing(set_of("FLUTE")) && !smp_set_missing(piano) && !smp_set_missing(set_of("STRGS")) &&
       !smp_set_missing(set_of("SCRCH")) && !smp_set_missing(set_of("PERC")));
    ok("sets: every set keeps its index and name (projects and presets point at the same sets)",
       SMP_NSETS == 8 && bass == 1u && str_eq(SMP_ALL_NAMES[5], "FLUTE"));
    pk = play(bass, &c);
    /* 32 blocks of 32 samples = 1024 samples = 23.2 ms of 440 Hz: ~20 zero crossings */
    ok("SAMPLE on BASS (left out) -> a sine at the note's pitch (440 Hz: ~20 crossings in 23 ms)", pk > 200 && c >= 18u && c <= 23u);
    pk = play(piano, &c);
    ok("SAMPLE on PIANO (in) -> its samples", pk > 200);
    ui.msg_t = 0;
    smp_notice();
    ok("the default track 3 (LOFI FLUTE, a SAMPLE preset on FLUTE) -> NO SAMPLE at power-on (it plays the sine)",
       ui.msg_t && str_eq(ui.msg, "NO SAMPLE") && ENGINES[trk[2].eng_req % NENGINES] == &ENG_SAMPLE);
    trk[1].eng_req = trk[2].eng_req = 0;            /* (the other tracks on ANALOG: track 1's notices alone) */
    smp_notice();
    ui.msg_t = 0;
    trk[0].p[P_E0] = (int16_t)bass;
    smp_notice();
    ok("NO SAMPLE on the top bar for a sound on a set left out", ui.msg_t && str_eq(ui.msg, "NO SAMPLE"));
    ui.msg_t = 0;
    smp_notice();
    ok("NO SAMPLE once (the same sound: not again)", !ui.msg_t);
    trk[0].p[P_E0] = (int16_t)piano;
    smp_notice();
    trk[0].p[P_E0] = (int16_t)set_of("HORNS");
    smp_notice();
    ok("NO SAMPLE again for another set left out", ui.msg_t && str_eq(ui.msg, "NO SAMPLE"));
    printf(fails ? "bt_sample: %d FAILED\n" : "bt_sample: all checks passed\n", fails);
    return fails != 0;
}
