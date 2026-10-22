/* SPDX-License-Identifier: GPL-3.0-only
 * FM-Link: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after bt_midi.c): FM-Link's glue (docs/superpowers/specs/2026-10-21-fm-link-design.md
 * in Felucca), the engine (fmlink.c, Felucca's, unchanged: FM-1s on different firmwares keep time together) with this
 * FM-1's clock, sequencer and Bluetooth link. GLO > SYSTEM > SYNC INT leads, SYNC LINK (G_SYNC 4) follows; on another
 * SYNC the FM-1 takes no part (its HELLOs still say it is an FM-1).
 *   The audio ISR: bt_fml_block (seq.c events_block, before the transport's requests) with the tick its block is heard
 *   at (audio.c bt_heard_tick, as Felucca's kb_out_tick): the UI's PLAY / STOP in, what to act on out (a leader's PLAY
 *   with a follower linked: both start 80 ms later), the sequencer's advance in samples (a follower: a sample more or
 *   less a block, held on the leader; seq.c turns it into clock units).
 *   The main loop: bt_fml_frame (core_ble_api.c bt_service): the role from SYNC, the messages out; core_fml_rx: in (the
 *   follower's BPM set from the leader's at once, so the next block runs at it).
 * The engine's words are shared with the audio ISR: the main loop's calls run with interrupts off. */
#define BT_FML_C 1
#include "fmlink.c"
static fml_t bt_fml;
#ifndef BT_FML_HEARD
#define BT_FML_HEARD bt_heard_tick                   /* audio.c: the tick the block being rendered is heard at (the host
                                                      * tests define their own) */
#endif
static struct {
    uint8_t b[20], len;                              /* a message ble_fml_send had no room for: first, next pass */
    uint32_t ms;                                     /* when it was made: dropped 50 ms on (a stale time or state) */
    uint8_t said;                                    /* the peer announced (ui_message) while linked */
} bt_fo;

static uint32_t bt_fml_role(uint32_t sync)
{
    return sync == 0u ? FML_LEADER : sync == SYNC_LINK ? FML_FOLLOWER : FML_NONE;
}

/* the audio ISR, every block: the transport's request as FM-Link wants it, the sequencer's advance in *seq_n (samples).
 * SLOOP's requests: 1 PLAY, 2 STOP, 3 a start the master counts (a MIDI START: never a count-in); a follower's start
 * is one of those */
static void bt_fml_block(uint32_t n, uint32_t *seq_n)
{
    uint32_t bpm = (uint32_t)song.g[G_BPM], req;
    if (bt_fml.role == FML_NONE || bt_fml.role != bt_fml_role((uint32_t)song.g[G_SYNC]))
        return;                                      /* (SYNC changed: the main loop sets the engine up first) */
    req = fml_block(&bt_fml, BT_FML_HEARD, n, bpm, (uint32_t)FS * 60u / bpm, song.playing, transport_req, seq_n);
    transport_req = (uint8_t)(req == 1u && bt_fml.role == FML_FOLLOWER ? 3u : req);
}

/* core_fml_rx (the BLE unit's ble_service): a message from the other FM-1 and the tick it came at */
UI_SMALL static void bt_fml_rx(const uint8_t *p, uint32_t len, uint32_t t)
{
    fm1_irq_off();
    fml_rx(&bt_fml, p, len, t);
    if (bt_fml.role == FML_FOLLOWER && bt_fml.l_valid && bt_fml.l_bpm >= GP[G_BPM].min && bt_fml.l_bpm <= GP[G_BPM].max)
        song.g[G_BPM] = bt_fml.l_bpm;               /* (CLK LINK: the leader's tempo) */
    fm1_irq_on();
}

/* the main loop (core_ble_api.c bt_service): the role from CLK, the messages out while linked */
UI_SMALL static void bt_fml_frame(void)
{
    uint32_t role = bt_fml_role((uint32_t)song.g[G_SYNC]), len;
    int linked;
    if (role != bt_fml.role) {
        fm1_irq_off();
        fml_init(&bt_fml, role);
        fm1_irq_on();
        bt_fo.len = 0;
        bt_fo.said = 0;
    }
    if (bt_fml.msg_follow) {
        bt_fml.msg_follow = 0;
        ui_message("FOLLOWING LINK");                /* (a linked follower's own PLAY / STOP: the leader's count) */
    }
    if (!ble_fml_ready()) {                          /* (FM-Link's own link, or the device's) */
        bt_fo.len = 0;
        return;
    }
    if (bt_fo.len && fm1_ms - bt_fo.ms > 50u)
        bt_fo.len = 0;
    for (;;) {
        if (!bt_fo.len) {
            fm1_irq_off();
            len = fml_poll(&bt_fml, fm1_ticks(), bt_fo.b);
            fm1_irq_on();
            if (!len)
                break;
            bt_fo.len = (uint8_t)len;
            bt_fo.ms = fm1_ms;
        }
        if (!ble_fml_send(bt_fo.b, bt_fo.len))
            break;                                   /* no room (or not an FM-1): this one first, next pass */
        bt_fo.len = 0;
    }
    linked = fml_linked(&bt_fml, fm1_ticks());
    if (linked && !bt_fo.said && role != FML_NONE)
        ui_message(role == FML_LEADER ? "FM-LINK: LEADING" : "FM-LINK: FOLLOWING");
    bt_fo.said = (uint8_t)linked;
}
