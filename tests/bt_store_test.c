/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The Bluetooth build's settings record (firmware/src/bt_store.c) against a simulated NOR (as tests/storage_test.c):
 * the settings object as SLOOP 2.8 writes it loads as before in the Bluetooth build (no record: the defaults) and
 * the object the Bluetooth build writes is, to SLOOP 2.8, its own (the same payload, its exact length, the same side
 * record); the record round-trips; a save cut off before its record falls back to the other copy's; a save by the
 * plain build (or an older SLOOP) leaves the record to the other copy; a record from a later version (longer)
 * is not taken; the newer of two wins. project.c settings_write's order is the one modelled: st_save3, then
 * bts_commit. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static uint32_t progs;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i, k;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    for (; n; off += k, s += k, n -= k) {          /* (fl_write: page by page) */
        k = 256u - (off & 0xFFu) < n ? 256u - (off & 0xFFu) : n;
        for (i = 0; i < k; i++)
            nor[off + i] &= s[i];
        progs++;
    }
    return 0;
}
#include "../firmware/src/storage.c"
static struct { uint8_t playing; } song;           /* (bt_store.c bts_changed: saved when stopped) */
static uint8_t transport_req, settings_later;
static uint32_t saves;
static void settings_save(void) { saves++; }
#include "../firmware/src/bt_store.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("bt_store: %-92s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
/* a settings save as SLOOP writes it: the payload (persist_t + SYN kits + MIDI part) and the side record */
static uint8_t pay[1600], side[80];
static int save_plain(uint8_t fill)
{
    memset(pay, fill, sizeof pay);
    memset(side, fill ^ 0x5A, sizeof side);
    st_side = side;
    st_side_len = sizeof side;
    return st_save3(OBJ_SETTINGS, pay, 88, pay + 88, 1464, pay + 1552, 40);
}
static int save_ble(uint8_t fill)                   /* project.c settings_write in the Bluetooth build */
{
    int rc = save_plain(fill);
    if (rc)
        return rc;
    return bts_commit();
}
static void power_on(void)                          /* RAM gone: the record read again at its first use */
{
    memset(&bts, 0, sizeof bts);
    bts_loaded = bts_dirty = 0;
}
/* what SLOOP 2.8 reads of the settings object: its payload, its exact length (dsu_boot), its side record */
static int reads_as_28(uint8_t fill)
{
    static uint8_t got[ST_PAYLOAD_MAX], sg[80];
    st_hdr_t h;
    uint32_t i;
    int n = st_load(OBJ_SETTINGS, got, sizeof got), c = st_current(OBJ_SETTINGS, &h);
    if (n != 1592 || c < 0 || h.len != 1592u || st_side_load(OBJ_SETTINGS, sg, sizeof sg))
        return 0;
    for (i = 0; i < 1592u; i++)
        if (got[i] != fill)
            return 0;
    for (i = 0; i < sizeof sg; i++)
        if (sg[i] != (uint8_t)(fill ^ 0x5A))
            return 0;
    return 1;
}

int main(void)
{
    static const uint8_t zero[sizeof(bts_t)];
    bts_t keep;
    memset(nor, 0xFF, sizeof nor);

    /* SLOOP 2.8's settings in the Bluetooth build */
    check("2.8 object: saved as 2.8 writes it", save_plain(0x11) == 0);
    power_on();
    check("2.8 object: the Bluetooth build reads no record: every Bluetooth setting at its default (all zero)",
          !memcmp(bts_get(), zero, sizeof zero));
    check("2.8 object: its settings read as before (payload, length, side record)", reads_as_28(0x11));

    /* the Bluetooth build's save: the record round-trips, SLOOP 2.8 reads the object as its own */
    bts.set[6] = 0x18u;
    bts.set[7] = 0xA7u;
    bts.sync = 4;
    memset(bts.mac, 0xC7, 6);
    bts.mac[6] = 0xA5u;
    memcpy(bts.dev[0], "\x01\x02\x03\x04\x05\x06\x80", 7);
    strcpy((char *)&bts.dev[0][8], "SMC-Mixer");
    memset(bts.rf, 0x3C, 68);
    bts.rf[71] = 0xA5u;
    bts_dirty = 1;
    check("ble save: the settings, then the record into the same copy", save_ble(0x22) == 0 && !bts_dirty);
    keep = bts;
    power_on();
    check("ble save: the record reads back as written (settings, SYNC, address, devices, the RF copy)",
          !memcmp(bts_get(), &keep, sizeof keep - 4u));
    check("ble save: SLOOP 2.8 reads the object as its own (payload, exact length, side record)", reads_as_28(0x22));
    {
        st_hdr_t a, b;
        int va = st_head(OBJ_SETTINGS, 0, &a) == 0, vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
        uint32_t c = vb && (!va || b.seq > a.seq) ? 1u : 0u, off = st_sector(OBJ_SETTINGS, c);
        check("ble save: the record lies past the payload (bytes 0xE00.. of the copy's sector), nothing between",
              ST_PAYLOAD_OFF + 1592u <= BTS_OFF && nor[off + ST_PAYLOAD_OFF + 1592u] == 0xFFu &&
              nor[off + BTS_OFF - 1u] == 0xFFu && !memcmp(nor + off + BTS_OFF, &keep, 4));
    }

    /* a save cut off after its commit, before its record: the other copy's record */
    bts.dev[1][0] = 0x99u;
    bts_dirty = 1;
    check("torn: the settings committed", save_plain(0x33) == 0);
    fail_after = 0;                                  /* .. then the power goes: the record never programmed */
    check("torn: the record's write fails, it stays to be saved", bts_commit() != 0 && bts_dirty);
    fail_after = -1;
    power_on();
    check("torn: the record of the copy before (the last one written whole) is read", !memcmp(bts_get(), &keep, sizeof keep - 4u));
    check("torn: the settings are the new ones", reads_as_28(0x33));

    /* the plain build (or SLOOP 2.8) saves: its copy has no record, the other copy's stands */
    bts_dirty = 1;
    check("plain after: the Bluetooth build's save", save_ble(0x44) == 0);
    keep = bts;
    check("plain after: one save by the plain build", save_plain(0x55) == 0);
    power_on();
    check("plain after: the Bluetooth build finds its record in the other copy", !memcmp(bts_get(), &keep, sizeof keep - 4u));
    check("plain after: the plain build's settings are taken", reads_as_28(0x55));
    check("plain after: a second save by the plain build (both copies its own): the record is gone (defaults)",
          save_plain(0x66) == 0 && (power_on(), !memcmp(bts_get(), zero, sizeof zero)));

    /* the newer of two records; a damaged one; a later version's longer record */
    bts = keep;
    bts.seq = 10;
    bts.sync = 3;
    check("seq: a save with sequence 11", save_ble(0x77) == 0);
    bts.sync = 4;
    check("seq: then 12 in the other copy", save_ble(0x78) == 0);
    power_on();
    check("seq: the newer wins", bts_get()->sync == 4u && bts.seq == 12u);
    {
        st_hdr_t a, b;
        int va = st_head(OBJ_SETTINGS, 0, &a) == 0, vb = st_head(OBJ_SETTINGS, 1, &b) == 0;
        uint32_t c = vb && (!va || b.seq > a.seq) ? 1u : 0u;
        nor[st_sector(OBJ_SETTINGS, c) + BTS_OFF + 20u] ^= 0x01u;   /* (a bit flipped in the newer: its CRC fails) */
        power_on();
        check("crc: a damaged newer record -> the older", bts_get()->sync == 3u && bts.seq == 11u);
        {   /* a later version's record: 16 bytes appended, its CRC its last 4 */
            uint8_t v2[sizeof(bts_t) + 16u], *at = nor + st_sector(OBJ_SETTINGS, c ^ 1u) + BTS_OFF;
            uint32_t crc;
            bts_t was = bts;
            memcpy(v2, &bts, sizeof bts - 4u);
            memset(v2 + sizeof bts - 4u, 0x6E, 16);
            ((bts_t *)(void *)v2)->len = (uint16_t)sizeof v2;
            ((bts_t *)(void *)v2)->ver = 2;
            ((bts_t *)(void *)v2)->seq = 20;
            crc = st_crc32(v2, sizeof v2 - 4u);
            memcpy(v2 + sizeof v2 - 4u, &crc, 4);
            memcpy(at, v2, sizeof v2);              /* (an erase-free stand-in for its save) */
            power_on();
            check("len: a later version's longer record -> this version's fields of it",
                  bts_get()->seq == 20u && bts.sync == was.sync && !memcmp(bts.rf, was.rf, sizeof bts.rf));
            at[sizeof v2 - 1u] ^= 0x80u;
            power_on();
            check("len: .. its CRC (its last 4 bytes) wrong -> not taken", bts_get()->seq != 20u);
        }
    }
    check("changed: a change while stopped saves at once", (power_on(), bts_changed(), saves == 1u && bts_dirty));
    song.playing = 1;
    check("changed: while playing it waits for the stop (settings_later)", (bts_changed(), saves == 1u && settings_later));
    printf(bad ? "bt_store: %d FAILED\n" : "bt_store: all checks passed\n", bad);
    return bad != 0;
}
