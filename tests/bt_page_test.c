/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The Bluetooth page on SLOOP (firmware/src/bt_page.c: the menu's sixth section, BT) through the real menu (ui_menu.c
 * draw_menu / menu_input), built with FELUCCA_BLE=1 on SLOOP's UI harness (tests/bt_host.h): SELECT to the BT tab and
 * back, the cursor, BLUETOOTH ON / OFF (the only start: ble_user_start), AUTOSTART, ROLE, MODE, RECONNECT, the saved
 * devices' lines (FORGET asked and confirmed on the state line, USE), + ADD A DEVICE (an 8 s scan, then the list stays;
 * NOTHING FOUND: OCT+ scans again; a pick saves and connects), the state line, OCT- closing the menu, AUTOSTART after a
 * clean boot only, the top bar's icon. Each state is drawn (the harness asserts every draw stays on the screen) and
 * written to DIR/bt-*.ppm (tests/run_tests.sh: build/ui_bt). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }
#include "bt_host.h"
#define ok(what, good) check((good), (what))
static void knob(uint32_t e, int32_t s) { encs[panel.enc[e]] = s; frame(); }
static void open_bt(void)                           /* HOME held: the menu; SELECT right to the last tab */
{
    uint32_t i;
    ui.menu = 1;
    ui.menu_sel = 0;
    ui.force = 1;
    frame();
    for (i = 0; i < MS_COUNT; i++)
        knob(EN_SELECT, 1);
}
static void state(char *o) { bt_state_text(o); }
static int says(const char *s) { char b[40]; state(b); return str_eq(b, s); }
static void shot(const char *name) { char b[64]; ui.force = 1; frame(); str_cpy(b, "bt-", sizeof b); str_cpy(b + 3, name, sizeof b - 3); ppm(b); }
static bt_dev_t mk(uint8_t a0, uint32_t kind, const char *name)
{
    bt_dev_t d;
    memset(&d, 0, sizeof d);
    d.addr[0] = a0;
    d.addr[1] = 0x3F;
    d.addr[5] = 0xC2;
    d.kind = (uint8_t)kind;
    str_cpy(d.name, name, sizeof d.name);
    return d;
}
static void seen(uint32_t i, const bt_dev_t *d, uint32_t midi)
{
    memset(&bs.dev[i], 0, sizeof bs.dev[i]);
    memcpy(bs.dev[i].addr, d->addr, 6);
    str_cpy(bs.dev[i].name, d->name, sizeof bs.dev[i].name);
    bs.dev[i].seen_ms = fm1_ms;
    bs.dev[i].midi = (uint8_t)midi;
    bs.dev[i].rssi = (int8_t)(-40 - (int)i * 7);
    if (bs.ndev <= i)
        bs.ndev = i + 1u;
}

int main(int argc, char **argv)
{
    bt_set_t s;
    bt_dev_t a = mk(0xA1, BT_KIND_CTRL, "SMC-Mixer"), f = mk(0xF1, BT_KIND_FM1, "Felucca FM-1"),
             x = mk(0xB7, BT_KIND_CTRL, "Xkey Air 25"), d;
    outdir = argc > 1 ? argv[1] : "build/host";
    bt_power_on();
    bt_ui_init();

    /* ---- the section: the last tab; SELECT back; the scan while it is shown */
    open_bt();
    ok("page: SELECT right to the last tab -> the BT section", ui.menu == 1u && mi_sec(ui.menu_sel) == MS_BT && btp.open);
    bt_devs_frame();
    ok("page: open (ROLE HOST) -> a continuous scan for the devices' ON / NEAR / AWAY", bh.scan_want == 2);
    ok("page: the state line says OFF (Bluetooth is off at power-on)", says("OFF"));
    shot("off");
    knob(EN_SELECT, -1);
    bt_devs_frame();
    ok("page: SELECT left -> SYSTEM, the page closed, no scan", mi_sec(ui.menu_sel) == MS_SYSTEM && !btp.open && !bh.scan_want);
    ui.force = 1;
    frame();
    ppm("bt-menu-system");                           /* (the tabs: SYS for SYSTEM, then BT) */
    knob(EN_SELECT, 1);
    ok("page: SELECT right again -> BT, the cursor on BLUETOOTH", btp.open && btp.sel == 0u);

    /* ---- BLUETOOTH: the only start; ON is not saved */
    bts_dirty = 0;
    knob(EN_K3, 1);
    ok("BLUETOOTH: any knob right -> ON: links wanted, the stack started (ble_user_start)", btp.on && bs.want == 1 &&
       bs.starts == 1u && bs.state == BLE_ST_STARTING && !bts_dirty);
    ok("BLUETOOTH: the state line says STARTING", says("STARTING"));
    shot("starting");
    bs.state = BLE_ST_IDLE;
    ok("BLUETOOTH: up, nothing saved -> ON, NO DEVICE SAVED", says("ON, NO DEVICE SAVED"));
    tap(B_OCTUP);
    ok("BLUETOOTH: OCT+ toggles -> OFF: links dropped (ble_ui_want 0), no second start", !btp.on && bs.want == 0 &&
       bs.starts == 1u);
    tap(B_OCTUP);
    ok("BLUETOOTH: OCT+ again -> ON (the stack is up: not started again)", btp.on && bs.starts == 1u);

    /* ---- the rows: AUTOSTART, ROLE, MODE, RECONNECT (saved in the Bluetooth record) */
    knob(EN_PRESET, 1);
    bts_dirty = 0;
    knob(EN_K1, 1);
    bt_set_get(&s);
    ok("AUTOSTART: knob right -> ON, saved", btp.sel == 1u && s.autostart && bts_dirty);
    knob(EN_PRESET, 1);
    knob(EN_K2, 1);
    bt_set_get(&s);
    ok("ROLE: knob right -> DEVICE, handed to the BLE unit", s.role && bs_role == 1);
    bt_devs_frame();
    ok("ROLE DEVICE: no device lines, no scan for them", bt_page_rows() == BT_SET_ROWS && !bh.scan_want);
    bs.state = BLE_ST_ADVERTISING;
    ok("ROLE DEVICE: the state line says WAITING FOR A HOST", says("WAITING FOR A HOST"));
    shot("device-waiting");
    bs.state = BLE_ST_SUBSCRIBED;
    str_cpy(conn_name, "D4:12:9A:00:3F:C2", sizeof conn_name);
    conn_up = 1;
    ok("ROLE DEVICE: a host connected -> CONNECTED and the host", says("CONNECTED D4:12:9A:00:3F:C2"));
    ok("icon: a host connected -> lit", bt_head_icon() == 2u);
    shot("device-connected");
    conn_up = 0;
    bs.state = BLE_ST_IDLE;
    tap(B_OCTUP);
    bt_set_get(&s);
    ok("ROLE: OCT+ toggles -> HOST", !s.role && bs_role == 0);
    knob(EN_PRESET, 1);
    knob(EN_K4, 1);
    knob(EN_K4, 1);
    bt_set_get(&s);
    ok("MODE: two clicks right -> MIDI", s.mode == BT_MODE_MIDI);
    knob(EN_K4, 5);
    bt_set_get(&s);
    ok("MODE: on -> stops at FM-LINK", s.mode == BT_MODE_LINK);
    tap(B_OCTUP);
    bt_set_get(&s);
    ok("MODE: OCT+ wraps -> AUTO", s.mode == BT_MODE_AUTO);
    knob(EN_PRESET, 1);
    knob(EN_K1, 1);
    knob(EN_K1, 1);
    bt_set_get(&s);
    ok("RECONNECT: two clicks right -> SLOW (the fifth row: the cursor's, any knob)", btp.sel == 4u &&
       s.rate == BT_RATE_SLOW);
    knob(EN_K1, -1);
    knob(EN_K1, -1);
    knob(EN_K1, -1);
    bt_set_get(&s);
    ok("RECONNECT: left -> stops at AUTO", s.rate == BT_RATE_AUTO);
    shot("rows");

    /* ---- the saved devices: a line each; FORGET; USE */
    bt_devs_add(&x);
    bt_devs_add(&f);
    bt_devs_add(&a);                                 /* a, f, x */
    seen(0, &a, 1);
    seen(1, &x, 1);
    bh.slot[BLE_SLOT_CTRL].st = BLE_LS_UP;
    memcpy(bh.slot[BLE_SLOT_CTRL].addr, a.addr, 6);
    ok("devices: a line each and + ADD A DEVICE", bt_page_rows() == BT_SET_ROWS + 3u + 1u);
    ok("devices: ON (linked), NEAR (around), AWAY", str_eq(bt_dev_value(0), "ON") && str_eq(bt_dev_value(2), "NEAR") &&
       str_eq(bt_dev_value(1), "AWAY"));
    {
        char lb[24];
        bt_devs_get(1, &d);
        bt_dev_label(&d, lb);
        ok("devices: an FM-1 as FM-1 and its address' last four digits", str_eq(lb, "FM-1 3FF1"));
    }
    ok("state: one link up -> ON, 1 CONNECTED", says("ON, 1 CONNECTED"));
    ok("icon: a link up -> lit", bt_head_icon() == 2u);
    knob(EN_PRESET, 1);                              /* the first device line */
    shot("devices");
    knob(EN_PRESET, 1);
    knob(EN_PRESET, 1);                              /* x: NEAR */
    tap(B_OCTUP);
    ok("USE: OCT+ on a device around -> to the top of the list", bt_devs_get(0, &d) && d.addr[0] == 0xB7);
    knob(EN_PRESET, -1);                             /* (a, f, x -> x, a, f: the cursor stays on the third line) */
    ok("USE: the list x, a, f", bt_devs_get(2, &d) && d.addr[0] == 0xF1);
    knob(EN_PRESET, 1);                              /* f: AWAY */
    tap(B_OCTUP);
    ok("USE: OCT+ on a device away -> NOT AROUND, nothing moves", str_eq(ui.msg, "NOT AROUND") && bt_devs_get(1, &d) &&
       d.addr[0] == 0xA1 && btp.sel == BT_SET_ROWS + 2u);
    knob(EN_K2, -1);
    ok("FORGET: a knob left on a device -> FORGET <name>? on the state line", btp.confirm == 3u && says("FORGET FM-1 3FF1?"));
    shot("forget");
    tap(B_OCTDN);
    ok("FORGET: OCT- -> no: kept, the menu still open", !btp.confirm && bt_devs_n() == 3u && ui.menu == 1u);
    knob(EN_K2, -1);
    tap(B_OCTUP);
    ok("FORGET: OCT+ -> yes: gone", !btp.confirm && bt_devs_n() == 2u);

    /* ---- + ADD A DEVICE: an 8 s scan, then the list stays; a pick saves it */
    btp.sel = (uint8_t)(BT_SET_ROWS + bt_devs_n());
    btp.on = 0;
    tap(B_OCTUP);
    ok("ADD: Bluetooth off -> BLUETOOTH IS OFF", !btp.add && str_eq(ui.msg, "BLUETOOTH IS OFF"));
    btp.on = 1;
    memset(&bs.dev, 0, sizeof bs.dev);
    bs.ndev = 0;
    tap(B_OCTUP);
    ok("ADD: OCT+ -> the scan list, SCANNING 8 S", btp.add && says("SCANNING 8 S"));
    frames(10);
    ok("ADD: nothing around yet -> an empty list", btp.an == 0u);
    shot("add-scanning");
    {
        bt_dev_t n1 = mk(0x51, BT_KIND_CTRL, "nanoKEY"), n2 = mk(0x52, BT_KIND_CTRL, "Speaker"), n3 = mk(0x53, BT_KIND_CTRL, "LPK25");
        seen(0, &n2, 0);                             /* not MIDI, strongest */
        seen(1, &n1, 1);
        seen(2, &a, 1);                              /* saved already */
        seen(3, &n3, 1);
        frame();
        ok("ADD: the devices not saved, BLE-MIDI ones first", btp.an == 3u && btp.adev[0].addr[0] == 0x51 &&
           btp.adev[1].addr[0] == 0x53 && btp.adev[2].addr[0] == 0x52);
        shot("add-list");
        fm1_ms += 9000u;
        bs.ndev = 0;
        frame();
        ok("ADD: after 8 s the list stays as it is (ADD: PICK A DEVICE)", btp.an == 3u && says("ADD: PICK A DEVICE"));
        knob(EN_PRESET, 1);
        tap(B_OCTUP);
        ok("ADD: OCT+ on one -> saved at the top, the list closed, the cursor on its line",
           !btp.add && bt_devs_get(0, &d) && d.addr[0] == 0x53 && btp.sel == BT_SET_ROWS);
    }
    btp.sel = (uint8_t)(BT_SET_ROWS + bt_devs_n());
    tap(B_OCTUP);
    fm1_ms += 9000u;
    frame();
    ok("ADD: nothing found -> NOTHING FOUND", says("NOTHING FOUND"));
    shot("add-none");
    tap(B_OCTUP);
    ok("ADD: OCT+ -> scans again", says("SCANNING 8 S"));
    tap(B_OCTDN);
    ok("ADD: OCT- -> back to the page (the menu still open)", !btp.add && ui.menu == 1u);

    /* ---- the state line's other states */
    bh.slot[BLE_SLOT_CTRL].st = BLE_LS_IDLE;
    ok("state: saved devices, none linked -> ON, SEARCHING", says("ON, SEARCHING"));
    ok("icon: on, nothing linked -> struck", bt_head_icon() == 1u);
    bs.state = BLE_ST_FAILED;
    bs.err = "NO RF TRIM IN FLASH";
    ok("state: FAILED and the reason", says("FAILED NO RF TRIM IN FLASH"));
    shot("failed");
    btp.on = 0;
    tap(B_OCTUP);                                    /* (the cursor on ADD) */
    btp.sel = 0;
    tap(B_OCTUP);
    ok("BLUETOOTH: after FAILED -> POWER CYCLE TO RETRY, no start", str_eq(ui.msg, "POWER CYCLE TO RETRY") &&
       bs.starts == 1u);
    bs.state = BLE_ST_IDLE;
    bs.err = 0;
    btp.on = 0;
    ok("icon: Bluetooth off -> none", bt_head_icon() == 0u);

    /* ---- OCT- closes the menu, as in every section */
    btp.sel = 0;
    tap(B_OCTDN);
    bt_devs_frame();
    ok("OCT- -> the menu closed, the page's scan ended", ui.menu == 0u && !btp.open && !bh.scan_want);
    {   /* the top bar's icon in SLOOP's head */
        btp.on = 1;
        bh.slot[BLE_SLOT_CTRL].st = BLE_LS_UP;
        ui.msg_t = 0;
        go_home();
        ui.force = 1;
        frame();
        ppm("bt-head-lit");                          /* (the TRACKS screen's header: ui_studio.c te_header) */
        bh.slot[BLE_SLOT_CTRL].st = BLE_LS_IDLE;
        ui.force = 1;
        frame();
        ppm("bt-head-struck");
        open_family(FAM_ENV);
        ui.force = 1;
        frame();
        ppm("bt-head-page-struck");                  /* (a page's top bar: ui_draw.c draw_head) */
        bh.slot[BLE_SLOT_CTRL].st = BLE_LS_UP;
        ui.force = 1;
        frame();
        ppm("bt-head-page-lit");
        go_home();
        btp.on = 0;
        ok("icon: drawn in the top bar (lit, struck) without leaving the screen", 1);
    }

    /* ---- AUTOSTART: once, 3 s after the first pass, after a clean boot only */
    bt_power_on();
    memset(&s, 0, sizeof s);
    s.autostart = 1;
    bt_set_put(&s);
    bt_page_auto();
    fm1_ms += 2000u;
    bt_page_auto();
    ok("AUTOSTART: not before 3 s", !bs.starts);
    fm1_ms += 1500u;
    bt_page_auto();
    ok("AUTOSTART: 3 s on, a clean boot -> ON (as the page's)", btp.on && bs.starts == 1u);
    fm1_ms += 60000u;
    bt_page_auto();
    ok("AUTOSTART: once", bs.starts == 1u);
    bt_power_on();
    bt_set_put(&s);
    bt_test_unclean = 1;
    bt_page_auto();
    fm1_ms += 4000u;
    bt_page_auto();
    ok("AUTOSTART: after a crash, a hang or a failed boot -> skipped, BT AUTOSTART SKIPPED", !bs.starts &&
       str_eq(ui.msg, "BT AUTOSTART SKIPPED"));
    bt_test_unclean = 0;
    bt_power_on();
    bt_page_auto();
    fm1_ms += 4000u;
    bt_page_auto();
    ok("AUTOSTART OFF (the default): nothing starts", !bs.starts && !btp.on);
    printf(fails ? "bt_page: %d FAILED\n" : "bt_page: all checks passed\n", fails);
    return fails != 0;
}
