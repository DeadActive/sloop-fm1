/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * FM-Link: 2026 DEADACTIVE */
/* FELUCCA_BLE: what the Bluetooth page keeps over power-off, in the Bluetooth record (bt_store.c bts.set, 8 bytes in
 * Felucca's layout):
 *   [0..5] retired (Felucca's one remembered device, before the saved devices: bt_devs.c; written 0), [6] bits 0-1
 *   MODE, bit 2 retired, bit 3 AUTOSTART (Bluetooth on by itself after a clean boot, bt_page.c bt_page_auto), bit 4
 *   ROLE (0 HOST, 1 DEVICE), bit 5 retired (written 0), bits 6-7 RECONNECT (0 AUTO, 1 FAST, 2 SLOW; 3 a damaged
 *   record), [7] the XOR of the seven bytes before it, XOR 0xB7.
 * A record without the bytes (none yet: all zero) or with a wrong check byte is MODE AUTO, AUTOSTART OFF, ROLE HOST,
 * RECONNECT AUTO. Main loop only. */
#define BT_SET_C 1
enum { BT_MODE_AUTO, BT_MODE_SMC, BT_MODE_MIDI, BT_MODE_LINK };   /* LINK: FM-Link only, no MIDI either way */
enum { BT_RATE_AUTO, BT_RATE_FAST, BT_RATE_SLOW };   /* RECONNECT: a host link's rest after a drop (bt_set_slow) */
typedef struct { uint8_t addr[6], type, mode, has, autostart, role, link, rate; } bt_set_t;   /* has: a device is
                                                                  * remembered; role: 1 DEVICE; link: HOST + LINK */
#define bt_set_bytes (bts_get()->set)

UI_SMALL static uint8_t bt_set_sum(const uint8_t *b)
{
    uint32_t i, x = 0xB7u;
    for (i = 0; i < 7u; i++)
        x ^= b[i];
    return (uint8_t)x;
}

UI_SMALL static void bt_set_get(bt_set_t *s)
{
    const uint8_t *b = bt_set_bytes;
    uint32_t i;
    memset(s, 0, sizeof *s);
    if (b[7] != bt_set_sum(b) || b[6] >= 0xC0u)
        return;
    for (i = 0; i < 6u; i++) {
        s->addr[i] = b[i];
        s->has |= b[i] != 0u;
    }
    s->type = (b[6] >> 2) & 1u;
    s->mode = b[6] & 3u;
    s->autostart = (b[6] >> 3) & 1u;
    s->role = (b[6] >> 4) & 1u;
    s->link = (b[6] >> 5) & 1u;
    s->rate = b[6] >> 6;
}

/* the bytes written and saved with the settings (bt_store.c bts_changed: at once, or once the transport stops) */
UI_SMALL static void bt_set_put(const bt_set_t *s)
{
    uint8_t *b = bt_set_bytes;
    uint32_t i;
    for (i = 0; i < 6u; i++)
        b[i] = s->addr[i];
    b[6] = (uint8_t)((s->mode <= BT_MODE_LINK ? s->mode : BT_MODE_AUTO) | (s->type ? 4u : 0u) | (s->autostart ? 8u : 0u) |
                     (s->role ? 16u : 0u) | (s->link ? 32u : 0u) | (uint32_t)s->rate << 6);
    b[7] = bt_set_sum(b);
    bts_changed();
}

/* RECONNECT: a host link rests longer after a drop or a failed connect (ble_fail_rest: 8 s, then 16 s): SLOW, or AUTO
 * while the transport plays (the user: a mixer that dropped refuses for ~8 s, each refused try holds the audio off
 * ~12 ms); FAST, or AUTO stopped: 2 s doubling to 16 s */
UI_SMALL static int bt_set_slow(int playing)
{
    bt_set_t s;
    bt_set_get(&s);
    return s.rate == BT_RATE_SLOW || (s.rate == BT_RATE_AUTO && playing);
}

/* SYNC (G_SYNC) BLE and LINK: the settings word keeps INT for them (panel.c lights_word, which the plain build reads),
 * the Bluetooth record the value (bt_store.c bts.sync). Once, at the first service: a kept BLE / LINK back (the
 * settings word said INT). Then every pass: a change of SYNC to or from BLE / LINK is kept */
UI_SMALL static void bt_sync_frame(void)
{
    static uint8_t done;
    bts_t *r = bts_get();
    uint32_t v = (uint32_t)song.g[G_SYNC];
    if (!done) {
        done = 1;
        if ((r->sync == 3u || r->sync == SYNC_LINK) && !v) {
            song.g[G_SYNC] = (int16_t)r->sync;
            lights_sync = r->sync;                   /* (project.c sections_flush: nothing changed, nothing saved) */
            return;
        }
    }
    v = v == 3u || v == SYNC_LINK ? v : 0u;
    if (v != r->sync) {
        r->sync = (uint8_t)v;
        bts_changed();
    }
}
