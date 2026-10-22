/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The SMC-Mixer DAW layout (firmware/src/smc.c) on SLOOP's four tracks, built with FELUCCA_BLE=1 on SLOOP's UI harness
 * (tests/bt_host.h): the decode (pitch bend faders, sign-magnitude knobs, the note rows), each action, the fader pickup,
 * the LEDs (only changes, everything on a new link), SOLO (SLOOP's song.solo), and what happens when the mixer goes or
 * MODE leaves SMC DAW. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint8_t sent[256][3];
static uint32_t nsent, room, refused;
int ble_midi_send(const uint8_t *m, uint32_t n)
{
    if (!room || nsent >= 256u || n != 3u) {
        refused++;
        return 0;
    }
    memcpy(sent[nsent++], m, 3);
    room--;
    return 1;
}
uint32_t ble_midi_room(void) { return room; }
#include "bt_host.h"
#define ok(what, good) check((good), (what))
static uint32_t note(uint32_t n, uint32_t vel) { return 0x09u | 0x90u << 8 | n << 16 | vel << 24; }
static uint32_t bend(uint32_t ch, uint32_t v7) { return 0x0Eu | (0xE0u | ch) << 8 | (v7 & 1u ? 0x7Fu : 0u) << 16 | v7 << 24; }
static uint32_t cc(uint32_t n, uint32_t v) { return 0x0Bu | 0xB0u << 8 | n << 16 | v << 24; }
static void drain(void) { room = 1000; smc_frame(); }
static void set_mode(uint32_t mode)
{
    bt_set_t s;
    bt_set_get(&s);
    s.mode = (uint8_t)mode;
    bt_set_put(&s);
}
static void link_up(const char *name)
{
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, name, sizeof conn_name);
    conn_up = 1;
    bt_link(1);
    smc_link(1);
}
static void link_down(void)
{
    conn_up = 0;
    bt_link(0);
    smc_link(0);
}
static void fresh(void)                              /* power-on, the SMC-Mixer linked, its LEDs and positions sent */
{
    bt_power_on();
    bt_ui_init();
    link_down();
    smc_frame();
    set_mode(BT_MODE_AUTO);
    link_up("SMC-Mixer");
    drain();
    nsent = refused = 0;
}
static int sent_has(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t i;
    for (i = 0; i < nsent; i++)
        if (sent[i][0] == a && sent[i][1] == b && sent[i][2] == c)
            return 1;
    return 0;
}

int main(void)
{
    uint32_t t;
    int16_t *vp;
    const param_desc_t *d;

    /* ---- faders: LEVEL 1..4 (the drum track's GLO > DRUMS LVL), REV 5..8, with pickup */
    fresh();
    trk[0].p[P_LEVEL] = 127;
    drain();
    smc_in(0x0Eu | 0xE0u << 8 | 0x7Fu << 16 | 0x7Fu << 24);
    smc_in(0x0Eu | 0xE0u << 8 | 0x00u << 16 | 0x40u << 24);
    ok("smc: a fader's 14-bit pitch bend is its top 7 bits: 8192 -> 64", trk[0].p[P_LEVEL] == 64);
    for (t = 0; t < 3u; t++) {
        fresh();
        smc_in(bend(t, (uint32_t)trk[t].p[P_LEVEL]));   /* at the track's LEVEL: picked up */
        smc_in(bend(t, 60 + t));
        ok("smc: fader 1..3 sets its synth track's LEVEL", trk[t].p[P_LEVEL] == (int16_t)(60 + t));
        smc_in(bend(4u + t, (uint32_t)trk[t].p[P_REV]));
        smc_in(bend(4u + t, 100 + t));
        ok("smc: fader 5..7 sets track 1..3's REV send", trk[t].p[P_REV] == (int16_t)(100 + t));
    }
    fresh();
    smc_in(bend(3, (uint32_t)song.g[G_DRLVL]));
    smc_in(bend(3, 77));
    smc_in(bend(7, (uint32_t)song.g[G_DRREV]));
    smc_in(bend(7, 33));
    ok("smc: faders 4 and 8: the drum track's LVL and REV (GLO > DRUMS, as its HOME)", song.g[G_DRLVL] == 77 &&
       song.g[G_DRREV] == 33);
    fresh();
    trk[1].p[P_LEVEL] = 100;
    drain();
    smc_in(bend(1, 20));
    ok("smc: pickup: a fader far from the value does nothing", trk[1].p[P_LEVEL] == 100);
    smc_in(bend(1, 99));
    smc_in(bend(1, 90));
    ok("smc: pickup: within 2 it takes the value, then follows", trk[1].p[P_LEVEL] == 90);
    trk[1].p[P_LEVEL] = 30;                          /* changed on the FM-1 */
    drain();
    smc_in(bend(1, 89));
    ok("smc: a value changed on the FM-1 releases the fader (no jump)", trk[1].p[P_LEVEL] == 30);
    ok("smc: .. and its new position is sent to the mixer", sent_has(0xE1, 30, 30));
    smc_in(bend(1, 20));
    smc_in(bend(1, 40));                             /* (passed through the value) */
    smc_in(bend(1, 41));
    ok("smc: pickup: passing through the value picks it up", trk[1].p[P_LEVEL] == 41);
    {
        static track_t before[NTRK];
        fresh();
        memcpy(before, trk, sizeof before);
        smc_in(bend(8, 0));
        smc_in(bend(9, 0));
        ok("smc: the master fader (channel 9) and the channels above do nothing", !memcmp(before, trk, sizeof before));
    }

    /* ---- knobs 1..4: the selected track's HOME parameters, a value a click; 5..8 MIDI LEARN's */
    fresh();
    song.sel = 0;
    d = home_param(1, &vp);
    *vp = (int16_t)((d->min + d->max) / 2);
    {
        int16_t v0 = *vp;
        smc_in(cc(17, 3));
        ok("smc: knob 2 right 3 clicks -> HOME KNOB 2's parameter + 3", *vp == v0 + 3);
        smc_in(cc(17, 0x40u | 5u));
        ok("smc: .. left 5 clicks -> - 5", *vp == v0 - 2);
        ok("smc: .. the top bar says the track, the parameter and its value", ui.msg_t && ui.msg[0] == 'T' && ui.msg[1] == '1');
    }
    song.sel = TRK_DRUM;
    {
        int16_t lv = song.g[G_DRLVL];
        smc_in(cc(16, 2));
        ok("smc: the drum track's knob 1: GLO > DRUMS LVL (its HOME)", song.g[G_DRLVL] == (int16_t)clamp(lv + 2, 0, 127));
    }
    song.sel = 0;
    {
        int16_t c0 = trk[0].p[P_TFLT];
        ml_tab[0] = (uint16_t)(20u | 0u << 7 | ml_code(P_TFLT) << 9);   /* CC 20 learned to track 1's FILTER */
        smc_in(cc(20, 1));
        ok("smc: knob 5 learned (CC 20) -> its parameter one value on from where it is", trk[0].p[P_TFLT] == c0 + 1);
        ml_tab[0] = 0;
        c0 = trk[0].p[P_TFLT];
        smc_in(cc(21, 1));
        ok("smc: knob 6 not learned -> nothing", trk[0].p[P_TFLT] == c0);
        ml_arm = 1;
        ml_heard = 0;
        smc_in(cc(22, 1));
        ok("smc: knob 7 while MIDI LEARN waits -> heard as CC 22", ml_heard == 23u);
        ml_arm = 0;
        ml_heard = 0;
    }

    /* ---- buttons */
    fresh();
    smc_in(note(0x18 + 2, 127));
    ok("smc: SELECT 3 -> track 3", song.sel == 2u);
    smc_in(note(0x10 + 1, 127));
    ok("smc: MUTE 2 -> track 2 muted", trk[1].p[P_MUTE] == 1);
    smc_in(note(0x10 + 1, 127));
    ok("smc: MUTE 2 again -> unmuted", trk[1].p[P_MUTE] == 0);
    smc_in(note(0x08 + 3, 127));
    ok("smc: SOLO 4 -> the drum track soloed (SLOOP's solo: the others silent)", song.solo == 8u && trk_silent(&trk[0]) &&
       !trk_silent(&trk[3]));
    smc_in(note(0x08 + 3, 0));
    ok("smc: a release (velocity 0) does nothing", song.solo == 8u);
    smc_in(note(0x08 + 3, 127));
    ok("smc: SOLO 4 again -> unsoloed", song.solo == 0u);
    smc_in(note(0x00 + 1, 127));
    ok("smc: REC 2 -> track 2 selected and armed (as REC on the FM-1)", song.sel == 1u && (song.rec || rec_wait));
    smc_in(note(0x5F, 127));
    smc_in(note(0x5E, 127));
    ok("smc: PLAY -> play", transport_req == 1u);
    frame();
    smc_in(note(0x5D, 127));
    ok("smc: STOP -> stop", transport_req == 2u);
    frame();
    smc_in(note(0x2F, 127));
    ok("smc: BANK > -> the next track", song.sel == 2u);
    smc_in(note(0x30, 127));
    smc_in(note(0x30, 127));
    smc_in(note(0x30, 127));
    ok("smc: CHANNEL < three times -> wraps to track 4", song.sel == 3u);
    smc_in(note(0x18 + 6, 127));
    ok("smc: strips 5..8's buttons do nothing", song.sel == 3u);

    /* ---- the LEDs: everything on a new link, then only changes */
    fresh();
    room = 1000;
    link_down();
    smc_frame();
    nsent = 0;
    link_up("SMC-Mixer");
    smc_frame();
    ok("smc: a new link -> every LED (35) and every fader position (8)", nsent == 35u + 8u);
    nsent = 0;
    smc_frame();
    ok("smc: nothing changed -> nothing sent", nsent == 0u);
    trk[2].p[P_MUTE] = 1;
    smc_frame();
    ok("smc: track 3 muted on the FM-1 -> MUTE 3 lit, only that", nsent == 1u && sent_has(0x90, 0x12, 127));
    trk[2].p[P_MUTE] = 0;
    nsent = 0;
    room = 0;
    smc_frame();
    ok("smc: no room -> waits (refused, not lost)", !nsent);
    room = 10;
    smc_frame();
    ok("smc: room -> sent", sent_has(0x90, 0x12, 0));

    /* ---- the mixer gone, or MODE leaving SMC DAW: its solo ends; the LEDs off once */
    fresh();
    song.solo = 1u;                                  /* (the FM-1's own: GLO + a key) */
    smc_in(note(0x08 + 2, 127));                     /* the mixer's SOLO 3 */
    link_down();
    ok("smc: the mixer gone -> its solo ends, the FM-1's own stays", song.solo == 1u);
    song.solo = 0;
    fresh();
    smc_in(note(0x08 + 0, 127));
    set_mode(BT_MODE_MIDI);
    nsent = 0;
    drain();
    ok("smc: MODE leaves SMC DAW -> unsolo, every LED off once", song.solo == 0u && nsent == 35u && sent_has(0x90, 0x5D, 0));
    nsent = 0;
    drain();
    ok("smc: .. and then nothing", nsent == 0u);
    printf(fails ? "smc: %d FAILED\n" : "smc: all checks passed\n", fails);
    return fails != 0;
}
