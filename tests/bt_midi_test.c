/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The core's side of Bluetooth MIDI on SLOOP (firmware/src/bt_set.c, bt_midi.c, bt_store.c), built with FELUCCA_BLE=1 on
 * SLOOP's UI harness (tests/bt_host.h: the real sequencer and audio between frames): the page's settings in the
 * Bluetooth record (Felucca's bytes), MIDI mode into the input ring tagged source 3 (cable 3) with the USB filter and
 * the room kept for USB and TRS, a note played through the sequencer, GLO > SYSTEM > SYNC BLE (the device's clock and
 * START / STOP) and only then, the mode (MODE, AUTO by the connected name), FM-LINK mode (nothing either way), the
 * notes out to the device, the clock out on SYNC INT, the release of what the device held when the link drops, SYNC
 * BLE / LINK kept in the record (the settings word, which the plain build reads, says INT). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint8_t sent[512][3];
static uint32_t sent_n[512], nsent, room;
int ble_midi_send(const uint8_t *m, uint32_t n)
{
    if (!room || nsent >= 512u)
        return 0;
    memcpy(sent[nsent], m, n);
    sent_n[nsent++] = n;
    room--;
    return 1;
}
uint32_t ble_midi_room(void) { return room; }
#include "bt_host.h"
#define ok(what, good) check((good), (what))
static uint32_t msg(uint32_t st, uint32_t d1, uint32_t d2) { return (st >> 4) | st << 8 | d1 << 16 | d2 << 24; }
static int gate_note(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static void link_to(const char *name)
{
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, name, sizeof conn_name);
    conn_up = 1;
    bt_link(1);                                      /* (ble_service: core_ble_link(1) once linked) */
}
static void fresh(uint32_t mode)                     /* power-on, MODE, nothing connected */
{
    bt_set_t s;
    bt_power_on();
    bt_ui_init();
    memset(&s, 0, sizeof s);
    s.mode = (uint8_t)mode;
    bt_set_put(&s);
    conn_up = 0;
    bt_link(0);
    memset(&bt_in, 0, sizeof bt_in);
    nsent = 0;
    room = 0;
}
static uint32_t ring_n(void) { return mi_w - mi_r; }
static uint32_t bq_at(uint32_t k) { return midi_in_q[(mi_r + k) % MQ]; }
static uint32_t count_sent(uint32_t st)
{
    uint32_t i, n = 0;
    for (i = 0; i < nsent; i++)
        n += sent[i][0] == st;
    return n;
}

int main(void)
{
    bt_set_t s, g;
    uint32_t i, m, same;

    /* ---- the page's settings: Felucca's 8 bytes, in the Bluetooth record */
    bt_power_on();
    bt_set_get(&g);
    ok("settings: an empty record is MODE AUTO, AUTOSTART OFF, ROLE HOST, RECONNECT AUTO",
       g.mode == BT_MODE_AUTO && !g.autostart && !g.role && !g.rate);
    for (m = 0, same = 1; m < 4u; m++) {
        memset(&s, 0, sizeof s);
        s.mode = (uint8_t)m;
        bt_set_put(&s);
        bt_set_get(&g);
        same &= g.mode == m;
    }
    ok("settings: MODE round-trips (AUTO, SMC DAW, MIDI, FM-LINK)", same);
    memset(&s, 0, sizeof s);
    s.mode = BT_MODE_SMC;
    s.autostart = 1;
    s.role = 1;
    s.rate = BT_RATE_SLOW;
    bts_dirty = 0;
    bt_set_put(&s);
    bt_set_get(&g);
    ok("settings: AUTOSTART, ROLE DEVICE, RECONNECT SLOW round-trip in Felucca's flags (0x99), saved",
       g.autostart && g.role && g.rate == BT_RATE_SLOW && g.mode == BT_MODE_SMC && bts.set[6] == 0x99u && bts_dirty);
    bts.set[6] |= 0xC0u;                             /* RECONNECT 3: no such value */
    bts.set[7] = bt_set_sum(bts.set);
    bt_set_get(&g);
    ok("settings: an unknown RECONNECT -> the defaults", !g.rate && g.mode == BT_MODE_AUTO && !g.autostart && !g.role);
    bt_set_put(&s);
    bts.set[6] ^= 0x08u;                             /* changed by something else: the check fails */
    bt_set_get(&g);
    ok("settings: a damaged record -> the defaults", g.mode == BT_MODE_AUTO && !g.autostart);
    s.rate = BT_RATE_AUTO;
    bt_set_put(&s);
    ok("RECONNECT AUTO: slow while playing, fast stopped", bt_set_slow(1) && !bt_set_slow(0));
    s.rate = BT_RATE_FAST;
    bt_set_put(&s);
    ok("RECONNECT FAST: fast while playing too", !bt_set_slow(1));

    /* ---- MIDI mode in: the ring as source 3, the filter, the room kept for USB and TRS */
    fresh(BT_MODE_AUTO);
    link_to("Keystep");
    ok("mode: AUTO with a device not named SMC -> MIDI", !bt_mode_now() && bt_in.out_on);
    bt_midi_in(msg(0x90, 60, 100));
    ok("in: a note-on goes into the input ring, tagged cable 3", ring_n() == 1u && bq_at(0) == (msg(0x90, 60, 100) | 0x30u));
    frame();
    ok("in: the sequencer plays it on track 1 (channel 1)", ring_n() == 0u && gate_note(&trk[0], 60));
    bt_midi_in(msg(0x80, 60, 0));
    frame();
    ok("in: its note-off ends it", !gate_note(&trk[0], 60));
    bt_midi_in(0x04u | 0xF0u << 8 | 0x7Du << 16 | 0x01u << 24);   /* SysEx start */
    bt_midi_in(0x09u | 0x90u << 8 | 0x80u << 16);                   /* a data byte with bit 7 */
    bt_midi_in(0x09u | 0x80u << 8 | 60u << 16);                     /* CIN 9, status 8x: malformed */
    ok("in: SysEx and malformed packets are dropped (the USB filter)", ring_n() == 0u);
    bt_midi_in(0x0Fu | 0xF8u << 8);
    ok("in: the clock goes into the ring, cable 3", ring_n() == 1u && bq_at(0) == (0x3Fu | 0xF8u << 8));
    frame();
    for (i = 0; i < MQ - 9u; i++) {
        midi_in_q[mi_w % MQ] = msg(0xB0, 7, 1);
        mi_w++;
    }
    bt_midi_in(msg(0x90, 61, 90));
    ok("in: with 9 places left it takes one (8 stay for USB and TRS)", ring_n() == MQ - 8u && !bt_in.drops);
    bt_midi_in(msg(0x90, 62, 90));
    ok("in: with 8 left it refuses and counts", ring_n() == MQ - 8u && bt_in.drops == 1u);
    mi_r = mi_w;

    /* ---- SYNC BLE: the device's clock and START / STOP run the transport; only with SYNC BLE */
    fresh(BT_MODE_MIDI);
    link_to("Keystep");
    song.g[G_SYNC] = 3;
    bt_midi_in(0x0Fu | 0xFAu << 8);
    for (i = 0; i < 48u; i++) {
        bt_midi_in(0x0Fu | 0xF8u << 8);
        frame();
    }
    ok("SYNC BLE: START and pulses from the device -> playing, following its clock", song.playing && mclk_on());
    bt_midi_in(0x0Fu | 0xFCu << 8);
    frames(2);
    ok("SYNC BLE: STOP -> stopped", !song.playing);
    fresh(BT_MODE_MIDI);
    link_to("Keystep");
    song.g[G_SYNC] = 1;                              /* SYNC USB: the BLE clock is not the one */
    bt_midi_in(0x0Fu | 0xFAu << 8);
    for (i = 0; i < 24u; i++) {
        bt_midi_in(0x0Fu | 0xF8u << 8);
        frame();
    }
    ok("SYNC USB: the device's START and clock are ignored", !song.playing && !mclk_on());
    song.g[G_SYNC] = 0;

    /* ---- the mode: SMC DAW by name, FM-LINK nothing */
    fresh(BT_MODE_AUTO);
    link_to("SMC-Mixer");
    bt_midi_in(msg(0x90, 0x18, 127));               /* (SELECT 1: smc.c) */
    ok("mode: AUTO with an SMC device -> SMC DAW (not the ring), no MIDI out", bt_mode_now() && ring_n() == 0u &&
       !bt_in.out_on);
    fresh(BT_MODE_LINK);
    link_to("Keystep");
    bt_midi_in(msg(0x90, 60, 100));
    room = 8;
    key(key_of_white(7));
    bt_midi_frame();
    ok("mode: FM-LINK -> nothing in, nothing out", ring_n() == 0u && !nsent && !bt_in.out_on);

    /* ---- out: the notes sent to USB MIDI out go to the device too */
    fresh(BT_MODE_MIDI);
    link_to("Keystep");
    room = 0;
    fm1_in.notes |= 1u << key_of_white(7);
    frame();
    bt_midi_frame();
    ok("out: no room -> nothing sent yet (queued)", !nsent && bt_out_w != bt_out_r);
    room = 8;
    bt_midi_frame();
    ok("out: room -> the key's note-on on track 1's channel, 3 bytes", nsent == 1u && sent[0][0] == 0x90u &&
       sent_n[0] == 3u && sent[0][2]);
    fm1_in.notes &= ~(1u << key_of_white(7));
    frame();
    bt_midi_frame();
    ok("out: its note-off", nsent == 2u && sent[1][0] == 0x80u && sent[1][1] == sent[0][1]);
    bt_link(0);
    ok("out: the link down -> the queue dropped", bt_out_r == bt_out_w && !bt_in.out_on);

    /* ---- clock out: SYNC INT, playing, MIDI mode: Start, 24 pulses a beat, Stop */
    fresh(BT_MODE_MIDI);
    link_to("Ableton");
    room = 500;
    transport_req = 1;
    frame();
    bt_midi_frame();
    ok("clock out: PLAY -> Start, then the downbeat's pulse", nsent >= 2u && sent[0][0] == 0xFAu && sent[1][0] == 0xF8u &&
       sent_n[0] == 1u);
    nsent = 0;
    {
        uint32_t f, beats = 4, blocks = beats * 60u * FS / 120u / CTL;   /* (120 BPM: four beats of blocks) */
        for (f = 0; f < blocks; f++) {
            events_block(CTL);
            bt_midi_frame();
        }
        ok("clock out: 24 pulses a beat at the FM-1's tempo (4 beats: 96, one either way)",
           count_sent(0xF8u) >= 95u && count_sent(0xF8u) <= 97u);
    }
    transport_req = 2;
    frame();
    bt_midi_frame();
    ok("clock out: STOP -> Stop", sent[nsent - 1u][0] == 0xFCu);
    nsent = 0;
    song.g[G_SYNC] = 2;                              /* SYNC TRS: following a clock, none sent */
    transport_req = 1;
    frames(4);
    bt_midi_frame();
    ok("clock out: a FM-1 that follows a clock (SYNC TRS) sends none", !count_sent(0xF8u) && !count_sent(0xFAu));
    transport_req = 2;
    frame();
    song.g[G_SYNC] = 0;

    /* ---- the link down: what the device held is released */
    fresh(BT_MODE_MIDI);
    link_to("Keystep");
    bt_midi_in(msg(0x92, 64, 100));                 /* channel 3 */
    bt_midi_in(msg(0xB4, 64, 127));                 /* the pedal on channel 5 */
    mi_r = mi_w;
    bt_link(0);
    bt_midi_frame();
    ok("drop: the pedal up and All Notes Off on each channel it played (3 and 5)", ring_n() == 4u &&
       bq_at(0) == (msg(0xB2, 64, 0) | 0x30u) && bq_at(1) == (msg(0xB2, 123, 0) | 0x30u) &&
       bq_at(2) == (msg(0xB4, 64, 0) | 0x30u) && bq_at(3) == (msg(0xB4, 123, 0) | 0x30u));
    mi_r = mi_w;

    /* ---- SYNC BLE / LINK kept in the record; the settings word (the plain build's) says INT */
    fresh(BT_MODE_MIDI);
    song.g[G_SYNC] = 4;
    bts_dirty = 0;
    bt_sync_frame();
    ok("sync: SYNC LINK kept in the Bluetooth record, saved", bts.sync == 4u && bts_dirty);
    lights_sync = 4;
    ok("sync: the settings word says INT for it (the plain build: INT)", ((lights_word() >> 12) & 3u) == 0u);
    lights_sync = 3;
    ok("sync: .. and for BLE", ((lights_word() >> 12) & 3u) == 0u);
    lights_sync = 2;
    ok("sync: TRS as ever", ((lights_word() >> 12) & 3u) == 2u);
    song.g[G_SYNC] = 2;
    bt_sync_frame();
    ok("sync: SYNC TRS -> the record says none (the word keeps it)", bts.sync == 0u);
    printf(fails ? "bt_midi: %d FAILED\n" : "bt_midi: all checks passed\n", fails);
    return fails != 0;
}
