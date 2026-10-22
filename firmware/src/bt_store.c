/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after storage.c, before project.c): what the Bluetooth build keeps over power-off,
 * in a record of its own in each copy of the settings object, past its payload: BTS_OFF.. of the copy's sector
 * (storage.c st_sector(OBJ_SETTINGS, c): 0xFC000 / 0xFD000). Nothing an older SLOOP or the plain build reads: they
 * check the payload's exact length and the side record, both untouched (the settings object stays byte for byte
 * the one the plain build writes), and never look past the payload. So no existing record grows.
 *   The record: a magic, its length and version (a later version appends and keeps this prefix), a sequence number, the
 * Bluetooth page's settings (bt_set.c: MODE, AUTOSTART, ROLE, RECONNECT, in the reference's 8 bytes), SYNC BLE / LINK
 * (the settings word keeps 0..2 only: panel.c lights_word), this FM-1's Bluetooth address (bt_devs.c bt_mac), the 8
 * saved devices (24 bytes each), the radio calibration's copy (72 bytes: bt_devs.c bt_rf_io, ble/ble_vm.c), then a
 * CRC-32 of everything before it. 300 bytes.
 *   Written after each settings save (project.c settings_write: st_save3 commits the copy, then this record goes into
 * the erased bytes of that copy's sector), so each copy carries the record as it was at its save. Read: of the two
 * copies' records the valid one with the higher sequence number (a save cut off before its record was programmed, or
 * a save by the plain build or an older SLOOP, which leave the bytes erased, falls back to the other copy's).
 * Read once, at the first use (bts_get); a change is saved with the settings (bts_changed). Main loop only. */
#define BTS_OFF 0xE00u                              /* in the copy's sector: past every payload (asserted in project.c) */
#define BTS_MAGIC 0x31544253u                       /* "SBT1" */
#define BTS_VER 1u
typedef struct {
    uint32_t magic;
    uint16_t len, ver;                              /* sizeof, BTS_VER */
    uint32_t seq;
    uint8_t set[8];                                 /* bt_set.c */
    uint8_t sync, rsv[3];                           /* G_SYNC 3 BLE / 4 LINK (0: the settings word's) */
    uint8_t mac[8];                                 /* bt_devs.c bt_mac */
    uint8_t dev[8][24];                             /* bt_devs.c, the saved devices */
    uint8_t rf[72];                                 /* bt_devs.c bt_rf_io */
    uint32_t crc;                                   /* st_crc32 of the bytes before it */
} bts_t;
_Static_assert(sizeof(bts_t) == 300u && BTS_OFF + sizeof(bts_t) <= ST_SECTOR, "the Bluetooth record");
static bts_t bts;
static uint8_t bts_loaded, bts_dirty;               /* read from flash; changed since the last save */

UI_SMALL static int bts_valid(const bts_t *r)
{
    return r->magic == BTS_MAGIC && r->ver >= BTS_VER && r->len == sizeof *r && r->crc == st_crc32(r, sizeof *r - 4u);
}
/* the record of copy c (0 / 1) into r: 1 = a valid one */
UI_SMALL static int bts_read(uint32_t c, bts_t *r)
{
    return st_read(st_sector(OBJ_SETTINGS, c) + BTS_OFF, r, sizeof *r) == 0 && bts_valid(r);
}
UI_SMALL static void bts_load(void)                          /* the newer of the two copies' records, else none (all zero) */
{
    bts_t b;
    int va, vb;
    bts_loaded = 1;
    memset(&bts, 0, sizeof bts);
    va = bts_read(0, &bts);
    vb = bts_read(1, &b);
    if (vb && (!va || b.seq > bts.seq))
        bts = b;
    else if (!va)
        memset(&bts, 0, sizeof bts);
}
UI_SMALL static bts_t *bts_get(void)
{
    if (!bts_loaded)
        bts_load();
    return &bts;
}
/* project.c settings_write, the copy just committed: this record into it (its bytes there are erased). 0 = written */
UI_SMALL static int bts_commit(void)
{
    st_hdr_t a, b;
    int va, vb;
    uint32_t c;
    (void)bts_get();
    va = st_head(OBJ_SETTINGS, 0, &a) == 0;
    vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
    if (!va && !vb)
        return -1;
    c = vb && (!va || b.seq > a.seq) ? 1u : 0u;     /* (the copy with the higher sequence: the one just written) */
    bts.magic = BTS_MAGIC;
    bts.len = (uint16_t)sizeof bts;
    bts.ver = BTS_VER;
    bts.seq++;
    bts.crc = st_crc32(&bts, sizeof bts - 4u);
    if (st_prog(st_sector(OBJ_SETTINGS, c) + BTS_OFF, &bts, sizeof bts))
        return -1;
    bts_dirty = 0;
    return 0;
}
/* a Bluetooth setting changed: saved with the settings, at once when stopped, else once stopped (a flash write
 * stops the audio for a moment: project.c sections_flush) */
UI_SMALL static void bts_changed(void)
{
    bts_dirty = 1;
    if (song.playing || transport_req)
        settings_later = 1;
    else
        settings_save();
}
