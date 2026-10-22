/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE (after Hortator's bt_midi.c)
 * FM-Link: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after bt_set.c and smc.c): the core's side of Bluetooth MIDI.
 * In: each USB-MIDI packet the BLE unit received goes to the SMC-Mixer layout (SMC DAW mode, smc.c) or, in MIDI mode,
 *   through the USB filter into the MIDI input ring tagged as source 3 (cable 3: usb.c's packets have cable 0, the TRS
 *   jack's realtime ones cable 1; seq.c events_block: ordinary MIDI from there: notes by channel, bend, sustain, CCs,
 *   Program Change, MIDI LEARN, and the clock and START / STOP with GLO > SYSTEM > SYNC = BLE).
 * Out, MIDI mode: the notes the FM-1 sends to USB MIDI out (seq.c midi_out_note: the keys on the track's channel, and
 *   the sequencer's with GLO > SYSTEM > MIDI = SEQ) also go to the connected device. Their callers run in the audio
 *   interrupt, so they queue here and the main loop hands the queue to the BLE unit.
 * Clock out: while the FM-1 runs on its own tempo (SYNC INT) and is linked in MIDI mode it is the master of the BLE
 *   device (another FM-1 with SYNC BLE, a computer): Start on PLAY, 24 clock pulses a beat counted in the sequencer's
 *   own clock units (so they never drift from its steps), Stop on STOP. A unit following a clock itself (SYNC USB /
 *   TRS / BLE / LINK) sends none: no loop. bt_clock_block, from seq.c events_block (the audio ISR).
 * FM-LINK mode: no MIDI either way (received packets dropped, nothing sent, no clock out): the link carries FM-Link
 *   only (bt_fml.c runs in every mode).
 * The link down: what the device held is released (CC 64 = 0 and CC 123 on each channel it played), as a pulled cable
 *   would otherwise leave its notes sounding. */
#define BT_MIDI_C 1
#define BT_OUT_Q 32u
static struct {
    uint32_t drops;                                  /* MIDI mode: packets refused, the ring's last 8 slots are TRS's
                                                      * and USB's */
    uint32_t out_drops;                              /* MIDI out: messages lost to a full queue here */
    uint16_t held, release;                          /* channels the device played / still to be released */
    uint8_t smc;                                     /* the linked device's name starts with "SMC" (bt_link) */
    uint8_t up;                                      /* linked (subscribed to the device's BLE-MIDI) */
    volatile uint8_t out_on;                         /* MIDI out wanted: linked, MIDI mode (bt_midi_frame) */
} bt_in;
static uint32_t bt_out_q[BT_OUT_Q];
static volatile uint32_t bt_out_w, bt_out_r;
static void smc_in(uint32_t pkt);                    /* smc.c */

/* 1 = the SMC-Mixer DAW layout, 0 = MIDI: MODE, or AUTO by the connected device's name */
UI_SMALL static uint32_t bt_mode_now(void)
{
    bt_set_t s;
    bt_set_get(&s);
    if (s.mode != BT_MODE_AUTO)
        return s.mode == BT_MODE_SMC;
    return bt_in.smc;
}
UI_SMALL static int bt_link_only(void)                        /* MODE FM-LINK: no MIDI either way */
{
    bt_set_t s;
    bt_set_get(&s);
    return s.mode == BT_MODE_LINK;
}

/* core_ble_link: the link up (the connected device's name read once: an auto scan waits up to 500 ms for the
 * target's name, ble_auto_step) / down */
UI_SMALL static void bt_link(int up)
{
    char n[24];
    bt_in.up = (uint8_t)(up != 0);
    bt_in.smc = (uint8_t)(up && ble_ui_conn_name(n) && n[0] == 'S' && n[1] == 'M' && n[2] == 'C');
    bt_in.out_on = (uint8_t)(up && !bt_mode_now() && !bt_link_only());   /* (at once: bt_midi_frame follows MODE from
                                                                           * then on) */
    if (!up) {
        bt_in.release |= bt_in.held;                 /* (bt_midi_frame: into the ring when it has room) */
        bt_in.held = 0;
        bt_out_r = bt_out_w;                         /* nothing queued for a device that is gone */
    }
}

/* usb.c midi_in_event's filter: the realtime F8 FA FB FC (as their status), channel voice with CIN = the status'
 * high nibble and data bytes < 0x80 (Cx Dx: one); 0 = dropped */
UI_SMALL static uint32_t bt_midi_filter(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin == 0xFu)
        return st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu ? 0xFu | st << 8 : 0u;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        return pkt;
    return 0;
}

/* one packet into the MIDI input ring (usb.c midi_in_q) as source 3 (cable 3); 0 = refused. 8 slots stay for USB and
 * TRS, which drop a packet when the ring is full. The TIMER5 ISR fills the ring as well (USB, TRS): interrupts off */
UI_SMALL static void bt_midi_q(uint32_t pkt)
{
    midi_in_q[mi_w % MQ] = (pkt & ~0xF0u) | 0x30u;
    RING_PUBLISH();
    mi_w++;
}
UI_SMALL static int bt_midi_put(uint32_t pkt)
{
    int took;
    fm1_irq_off();
    took = MQ - (mi_w - mi_r) > 8u;
    if (took)
        bt_midi_q(pkt);
    fm1_irq_on();
    return took;
}

/* two packets, both or neither (the release of a channel: the pedal up, then All Notes Off), in one interrupts-off
 * region so nothing from USB / TRS lands between them; 0 = refused (no room beside the 8 slots) */
UI_SMALL static int bt_midi_put2(uint32_t a, uint32_t b)
{
    int took;
    fm1_irq_off();
    took = MQ - (mi_w - mi_r) > 9u;
    if (took) {
        bt_midi_q(a);
        bt_midi_q(b);
    }
    fm1_irq_on();
    return took;
}

UI_SMALL static void bt_midi_in(uint32_t pkt)
{
    uint32_t cin, st;
    if (bt_link_only())
        return;                                      /* FM-LINK: MIDI from the device ignored */
    if (bt_mode_now()) {
        smc_in(pkt);
        return;
    }
    pkt = bt_midi_filter(pkt);
    if (!pkt)
        return;
    if (!bt_midi_put(pkt)) {
        bt_in.drops++;
        return;
    }
    cin = pkt & 15u;
    st = (pkt >> 8) & 0xFFu;
    if ((cin == 9u && (pkt >> 24)) || (cin == 0xBu && ((pkt >> 16) & 0xFFu) == 64u && (pkt >> 24)))
        bt_in.held |= (uint16_t)(1u << (st & 15u));  /* a note-on or the pedal down: this channel has something held */
}

static void bt_out_push(uint32_t pkt)               /* the out ring (one producer at a time: the audio ISR, or with
                                                     * interrupts off). Full: dropped and counted */
{
    if (bt_out_w - bt_out_r >= BT_OUT_Q) {
        bt_in.out_drops++;
        return;
    }
    bt_out_q[bt_out_w % BT_OUT_Q] = pkt;
    RING_PUBLISH();
    bt_out_w++;
}

/* clock out (the audio ISR, every block, as seq.c clkout_block: 24 pulses a beat, MCLK_PULSE_U clock units each) */
static struct { uint8_t run; uint32_t acc; } bt_clk;  /* acc: clock units since the last pulse */
static void bt_clock_block(uint32_t n)
{
    int run = song.playing && !song.g[G_SYNC] && bt_in.out_on;
    if (!run) {
        if (bt_clk.run && bt_in.out_on && !song.playing)
            bt_out_push(0xFu | 0xFCu << 8);         /* Stop */
        bt_clk.run = 0;
        return;
    }
    if (!bt_clk.run) {
        bt_out_push(0xFu | 0xFAu << 8);             /* Start, then the first beat's pulse */
        bt_out_push(0xFu | 0xF8u << 8);
        bt_clk.run = 1;
        bt_clk.acc = 0;
    }
    bt_clk.acc += n * (uint32_t)song.g[G_BPM];
    while (bt_clk.acc >= MCLK_PULSE_U) {
        bt_clk.acc -= MCLK_PULSE_U;
        bt_out_push(0xFu | 0xF8u << 8);
    }
}

/* a note the FM-1 sent to USB MIDI out (seq.c midi_out_note), for the BLE device too. From the audio interrupt: one
 * producer at a time. Full: dropped and counted */
static void bt_midi_out_isr(uint32_t pkt)
{
    uint32_t cin = pkt & 15u;
    if (!bt_in.out_on || cin < 8u || cin > 0xEu)
        return;
    bt_out_push(pkt);
}

/* the main loop (core_ble_api.c bt_service): the releases owed after a dropped link, then the out queue to the BLE
 * unit while it has room (ble_midi_send: main loop only) */
UI_SMALL static void bt_midi_frame(void)
{
    uint32_t ch;
    for (ch = 0; ch < 16u && bt_in.release; ch++) {
        if (!((bt_in.release >> ch) & 1u))
            continue;
        if (!bt_midi_put2(0x0Bu | (0xB0u | ch) << 8 | 64u << 16,       /* the pedal up, All Notes Off */
                          0x0Bu | (0xB0u | ch) << 8 | 123u << 16))
            break;                                   /* still owed: the next pass */
        bt_in.release &= (uint16_t)~(1u << ch);
    }
    bt_in.out_on = (uint8_t)(bt_in.up && !bt_mode_now() && !bt_link_only());
    while (bt_out_r != bt_out_w && ble_midi_room()) {
        uint32_t pkt = bt_out_q[bt_out_r % BT_OUT_Q], cin = pkt & 15u;
        uint8_t m[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        if (!ble_midi_send(m, cin == 0xFu ? 1u : cin == 0xCu || cin == 0xDu ? 2u : 3u))
            break;
        bt_out_r++;
    }
}
