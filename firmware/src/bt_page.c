/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE (after Felucca's bt_page.c, after Hortator's)
 * FM-Link: 2026 DEADACTIVE */
/* FELUCCA_BLE (felucca.c includes it after bt_set.c, bt_midi.c and bt_devs.c; ui_menu.c declares its entry points): the
 * Bluetooth page, the menu's sixth section, BT (HOME held, then SELECT to the last tab; ui_menu.c MS_BT). Felucca's
 * page (docs/superpowers/specs/2026-10-22-bt-devices-design.md 5.1, mockup A) in SLOOP's look: Terminus, the black
 * page, labels gray, values amber, white where the cursor is. A state line (and the last error), the rows BLUETOOTH,
 * AUTOSTART, ROLE (HOST, DEVICE), MODE (AUTO, SMC DAW, MIDI, FM-LINK) and RECONNECT (AUTO, FAST, SLOW: bt_set.c
 * bt_set_slow), then (HOST) the saved devices, a line each (a dot connected, a ring not, its name, CTRL or FM-1, ON /
 * NEAR / AWAY), three in view, and + ADD A DEVICE. SLOOP's menu sets a section's rows with KNOB 1..4, one each; this
 * section has more rows than knobs, so here (as on Felucca's page) PRESETS moves the cursor (it wraps) and any of
 * KNOB 1..4 changes the cursor's row, OCT+ acts on it. On a device line a knob left asks FORGET <name>? on the state
 * line (OCT+ yes, OCT- no) and OCT+ uses it now (the first of its kind); + ADD A DEVICE opens the scan list of nearby
 * devices not saved (BLE-MIDI ones first), found in BT_ADD_MS, then kept as they are: OCT+ saves and connects one,
 * OCT- goes back (NOTHING FOUND: OCT+ scans again). ROLE DEVICE: the FM-1 is the BLE-MIDI device a host connects to;
 * no device lines then. SELECT goes to the section before, OCT- closes the menu, as in every section.
 * BLUETOOTH ON is the only call of ble_user_start in the core (gate 3: tools/check_ble_boot.py TRIGGERS has
 * bt_page_on); Bluetooth is off at every power-on and ON is not saved, unless AUTOSTART is ON: then bt_page_auto
 * turns it on once, BT_AUTO_MS after the main loop began, after a clean boot only. AUTOSTART, ROLE, MODE, RECONNECT, the
 * saved devices are kept in the Bluetooth record (bt_store.c). Main loop only. */
#define BT_PAGE_C 1
#define BT_SET_ROWS 5u                              /* BLUETOOTH AUTOSTART ROLE MODE RECONNECT, then (HOST) the
                                                     * devices, ADD */
#define BT_SHOW 8u                                  /* the ADD list's lines (the scan keeps 12) */
#define BT_ADD_MS 8000u                             /* the ADD list's scan, then the list stays (the user: 8 s) */
#define BT_VIS 3u                                   /* device lines in view (the cursor scrolls them) */
#define BT_SY 24                                    /* (the menu's area: y 0..218 under the header) the state line */
#define BT_EY 40                                    /* the last error */
#define BT_RY 58                                    /* the rows, 17 px (Terminus 16 + 1), and the ADD list's */
#define BT_RH 17
#define BT_DY 146                                   /* the device lines */
#define BT_HY 202                                   /* the keys' line */
static const char *const BT_MODE_N[4] = {"AUTO", "SMC DAW", "MIDI", "FM-LINK"};
static const char *const BT_RATE_N[3] = {"AUTO", "FAST", "SLOW"};
UI_SMALL static uint32_t bt_role(void) { bt_set_t s; bt_set_get(&s); return s.role; }   /* 1: ROLE DEVICE */
static struct {
    uint8_t on;                                     /* BLUETOOTH ON (not saved) */
    uint8_t open;                                   /* the section is shown (bt_page_show) */
    uint8_t sel, top;                               /* the cursor (a row), the first device line in view */
    uint8_t confirm;                                /* FORGET asked for saved device confirm - 1 (0: none) */
    uint8_t add, asel, an;                          /* the ADD list open, its cursor, its devices */
    uint32_t add_t0;                                /* .. its scan's start (BT_ADD_MS: then it stays) */
    ble_ui_dev_t adev[BT_SHOW];                     /* the ADD list as drawn: a pick takes these */
} btp __attribute__((section(".pool")));

/* the top bar's Bluetooth icon (ui_draw.c draw_head; design 5.6): 0 none (Bluetooth off), 1 struck (on, nothing linked:
 * searching, reconnecting, the device role waiting), 2 lit (a link up) */
UI_SMALL static uint32_t bt_head_icon(void)
{
    ble_ui_slot_t x;
    uint32_t i, st = ble_ui_state();
    if (!btp.on)
        return 0;
    if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING)
        return 2;
    for (i = 0; i < 2u; i++)
        if (ble_ui_slot(i, &x) && x.st == BLE_LS_UP)
            return 2;
    return 1;
}

UI_SMALL static uint32_t bt_page_rows(void) { return bt_role() ? BT_SET_ROWS : BT_SET_ROWS + bt_devs_n() + 1u; }

/* a name as the screen can show it: printable ASCII, anything else '?' (a device's name is any bytes) */
UI_SMALL static void bt_name(char *o, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        o[i] = s[i] >= 0x20 && s[i] < 0x7F ? s[i] : '?';
    o[i] = 0;
}
UI_SMALL static char *bt_hex2(char *o, uint8_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4];
    *o++ = H[v & 15u];
    return o;
}
/* a saved device's line text (24 B): its name; an FM-1 (they all say Felucca FM-1) FM-1 and its address' last four
 * hex digits, most significant first */
UI_SMALL static void bt_dev_label(const bt_dev_t *d, char *o)
{
    if (d->kind == BT_KIND_FM1) {
        str_cpy(o, "FM-1 ", 24);
        *bt_hex2(bt_hex2(o + 5, d->addr[1]), d->addr[0]) = 0;
    } else {
        bt_name(o, d->name[0] ? d->name : "?", 24);
    }
}
UI_SMALL static const char *bt_dev_value(uint32_t i)        /* a device line's value: ON (linked), NEAR (around), AWAY */
{
    int st = bt_devs_state(i);
    return st == 2 ? "ON" : st ? "NEAR" : "AWAY";
}

UI_SMALL static void bt_state_text(char *o)                  /* the state line (40 B) */
{
    uint32_t st = ble_ui_state(), code, n, i;
    const char *e = ble_ui_err(&code);
    char t[24];
    bt_dev_t d;
    if (btp.confirm && bt_devs_get(btp.confirm - 1u, &d)) {
        bt_dev_label(&d, t);
        str_cpy(o, "FORGET ", 40);
        str_cpy(o + 7, t, 32);
        str_cpy(o + str_len(o), "?", 40 - str_len(o));
        return;
    }
    if (st == BLE_ST_FAILED) {
        str_cpy(o, "FAILED ", 40);
        str_cpy(o + 7, e, 33);
    } else if (st == BLE_ST_STARTING) {
        str_cpy(o, "STARTING", 40);
    } else if (!btp.on || st == BLE_ST_OFF) {
        str_cpy(o, "OFF", 40);
    } else if (st == BLE_ST_ADVERTISING) {
        str_cpy(o, "WAITING FOR A HOST", 40);
    } else if (st == BLE_ST_SUBSCRIBED || st == BLE_ST_RECEIVING) {   /* the device role: its host */
        str_cpy(o, "CONNECTED ", 40);
        if (ble_ui_conn_name(t))
            t[23] = 0, bt_name(o + 10, t, 30);
    } else if (btp.add) {                           /* its scan's seconds left, then what it found */
        n = BT_ADD_MS - (fm1_ms - btp.add_t0);
        if ((int32_t)n > 0) {
            str_cpy(o, "SCANNING ", 40);
            fmt_int(o + 9, (int32_t)((n + 999u) / 1000u));
            str_cpy(o + str_len(o), " S", 40 - str_len(o));
        } else {
            str_cpy(o, btp.an ? "ADD: PICK A DEVICE" : "NOTHING FOUND", 40);
        }
    } else if (!(n = bt_devs_n())) {
        str_cpy(o, "ON, NO DEVICE SAVED", 40);
    } else {
        for (i = 0, n = 0; i < 2u; i++) {
            ble_ui_slot_t x;
            n += ble_ui_slot(i, &x) && x.st == BLE_LS_UP;
        }
        if (n) {
            str_cpy(o, "ON, ", 40);
            fmt_int(o + 4, (int32_t)n);
            str_cpy(o + str_len(o), " CONNECTED", 40 - str_len(o));
        } else {
            str_cpy(o, "ON, SEARCHING", 40);
        }
    }
}

/* the ADD list: the BLE unit's scan list without the saved devices, BLE-MIDI devices first, each part by signal, the
 * first BT_SHOW; the cursor kept on the device it was on (the list re-sorts as signals change) */
UI_SMALL static int bt_saved(const uint8_t a[6])
{
    bt_dev_t d;
    uint32_t i;
    for (i = 0; bt_devs_get(i, &d); i++)
        if (!memcmp(d.addr, a, 6))
            return 1;
    return 0;
}
UI_SMALL static void bt_add_list(void)
{
    ble_ui_dev_t d;
    uint8_t was[6];
    uint32_t i, k, pass, n = ble_ui_devs(), had = btp.asel < btp.an;
    if (fm1_ms - btp.add_t0 >= BT_ADD_MS)
        return;                                     /* (its scan is over: the list stays as it is) */
    if (had)
        memcpy(was, btp.adev[btp.asel].addr, 6);
    btp.an = 0;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < n; i++) {
            if (!ble_ui_dev(i, &d) || (d.midi != 0) != (pass == 0u) || bt_saved(d.addr) || btp.an >= BT_SHOW)
                continue;
            d.name[sizeof d.name - 1u] = 0;
            btp.adev[btp.an++] = d;
        }
    for (k = 0; had && k < btp.an; k++)
        if (!memcmp(btp.adev[k].addr, was, 6))
            btp.asel = (uint8_t)k;
    if (btp.asel >= btp.an)
        btp.asel = btp.an ? (uint8_t)(btp.an - 1u) : 0u;
}
UI_SMALL static void bt_add_close(void) { btp.add = 0; }

/* BLUETOOTH ON: links wanted, the role handed over, the stack started if it is not (the user's trigger: the only
 * call of ble_user_start in the core, gate 3); the saved devices are the targets (bt_devs_frame) */
UI_SMALL __attribute__((noinline)) static void bt_page_on(void)
{
    if (ble_ui_state() == BLE_ST_FAILED) {          /* the stack cannot stop: only a power cycle starts it again */
        ui_message("POWER CYCLE TO RETRY");
        return;
    }
    if (btp.on)
        return;
    btp.on = 1;
    ble_ui_role((int)bt_role());                    /* (applied once the stack is up and idle) */
    ble_ui_want(1);
    if (ble_ui_state() == BLE_ST_OFF)
        ble_user_start();
}

/* AUTOSTART (core_ble_api.c bt_service, every main-loop pass): once per power-on, BT_AUTO_MS after the main loop
 * first served Bluetooth, BLUETOOTH ON as from the page, when AUTOSTART is ON and the boot was clean: no crash record
 * newer than the last boot saw (fm1_crash, the crash screen's reset), no WDT reset (a hang) and no failed boot
 * (bootguard: the last one died within its first 30 s). So a Bluetooth start that crashes or hangs the FM-1 is not
 * tried again on the next power-on: the way to UBOOT and the updater stays clear. Gate 3 (tools/check_ble_boot.py)
 * allows it only from the main loop's service */
#define BT_AUTO_MS 3000u
static struct { uint8_t armed, done, clean; uint32_t t0; } bt_auto;
#ifdef FM1_CRASH_MAGIC
static uint32_t bt_crash_seen __attribute__((section(".noinit")));   /* fm1_crash.count at the last boot */
UI_SMALL static int bt_boot_clean(void)
{
    int crashed = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != bt_crash_seen;
    bt_crash_seen = fm1_crash.count;
    return !crashed && !bootguard.failed && !(fm1_boot.p3_rst & 4u);
}
#else
static int bt_test_unclean;                         /* (the host tests: the boot as the test says) */
UI_SMALL static int bt_boot_clean(void) { return !bt_test_unclean; }
#endif
UI_SMALL __attribute__((noinline)) static void bt_page_auto(void)
{
    bt_set_t s;
    if (bt_auto.done)
        return;
    if (!bt_auto.armed) {
        bt_auto.armed = 1;
        bt_auto.t0 = fm1_ms;
        bt_auto.clean = (uint8_t)bt_boot_clean();   /* (read at the first pass, whatever AUTOSTART says: the record
                                                     * of the last boot moves on) */
        return;
    }
    if (fm1_ms - bt_auto.t0 < BT_AUTO_MS)
        return;
    bt_auto.done = 1;
    bt_set_get(&s);
    if (!s.autostart)
        return;
    if (!bt_auto.clean) {
        ui_message("BT AUTOSTART SKIPPED");          /* (after a crash, a hang or a failed boot) */
        return;
    }
    bt_page_on();
}

/* ui_menu.c: the BT section shown (1) or left (0; the menu closed too): the page's scan and list follow it */
UI_SMALL static void bt_page_show(int shown)
{
    if (shown && !btp.open) {
        btp.sel = btp.top = btp.confirm = 0;
        bt_add_close();
        bt_devs_page(!bt_role());                   /* (HOST: scan while the page is open: its ON / NEAR / AWAY) */
        ui.force = 1;
    } else if (!shown && btp.open) {
        btp.confirm = 0;
        bt_add_close();
        bt_devs_page(0);
    }
    btp.open = (uint8_t)(shown != 0);
}

/* the section's input (ui_menu.c menu_input, SELECT taken there): ok = OCT+, back = OCT-. 1 = the input was the page's;
 * 0 = OCT- with nothing open here: the menu closes */
UI_SMALL static int bt_page_input(uint32_t ok, uint32_t back)
{
    uint32_t rows = bt_page_rows(), st = ble_ui_state(), k, n = bt_devs_n();
    int32_t s;
    bt_set_t set;
    if (btp.add) {                                  /* the ADD list: PRESETS moves, OCT+ adds, OCT- back */
        if ((s = panel_enc(EN_PRESET)) != 0 && btp.an)
            btp.asel = (uint8_t)((btp.asel + (s > 0 ? 1u : btp.an - 1u)) % btp.an);
        if (back) {
            bt_add_close();
        } else if (ok && !btp.an) {
            btp.add_t0 = fm1_ms;                    /* (nothing found: OCT+ scans again) */
        } else if (ok && btp.asel < btp.an) {
            const ble_ui_dev_t *a = &btp.adev[btp.asel];
            bt_dev_t d;
            memset(&d, 0, sizeof d);
            memcpy(d.addr, a->addr, 6);
            d.type = a->type & 1u;
            d.kind = (uint8_t)bt_dev_kind(a->name);
            str_cpy(d.name, a->name, sizeof d.name);
            bt_devs_add(&d);                        /* (first of its kind: bt_devs_frame connects it) */
            bt_add_close();
            btp.sel = BT_SET_ROWS;                  /* on its line */
        }
        enc_drop();
        return 1;
    }
    if (btp.confirm) {                              /* FORGET <name>?: OCT+ yes, OCT- no; anything else no */
        for (s = panel_enc(EN_PRESET), k = 0; k < 4u; k++)
            s |= panel_enc(EN_K1 + k);
        if (!ok && !back && !s)
            return 1;                               /* (called every pass: waits for an answer) */
        k = btp.confirm - 1u;
        btp.confirm = 0;
        if (ok)
            bt_devs_forget(k);
        if (btp.sel >= bt_page_rows())
            btp.sel = (uint8_t)(bt_page_rows() - 1u);
        enc_drop();
        return 1;
    }
    if (back)
        return 0;
    if ((s = panel_enc(EN_PRESET)) != 0)            /* one row per detent, wrapping (as the menu's rows) */
        btp.sel = (uint8_t)((btp.sel + (s > 0 ? 1u : rows - 1u)) % rows);
    for (s = 0, k = 0; k < 4u; k++)                 /* KNOB 1..4, any of them */
        s += panel_enc(EN_K1 + k);
    switch (btp.sel) {
    case 0:                                         /* BLUETOOTH: a knob right ON, left OFF; OCT+ toggles */
        if (s > 0 || (ok && !btp.on)) {
            bt_page_on();
        } else if ((s < 0 || ok) && btp.on) {
            btp.on = 0;
            ble_ui_want(0);
        }
        break;
    case 1:                                         /* AUTOSTART: a knob right ON, left OFF; OCT+ toggles; saved */
        bt_set_get(&set);
        k = s > 0 ? 1u : s < 0 ? 0u : ok ? !set.autostart : set.autostart;
        if (k != set.autostart) {
            set.autostart = (uint8_t)k;
            bt_set_put(&set);
        }
        break;
    case 2:                                         /* ROLE: a knob right DEVICE, left HOST; OCT+ toggles; saved */
        bt_set_get(&set);
        k = s > 0 ? 1u : s < 0 ? 0u : ok ? !set.role : set.role;
        if (k != set.role) {
            set.role = (uint8_t)k;
            set.link = 0;
            bt_set_put(&set);
            ble_ui_role((int)k);                    /* (the BLE unit ends its links, a scan or advertising, then switches) */
            bt_devs_page(!k);
            btp.top = 0;
        }
        break;
    case 3:                                         /* MODE, RECONNECT: a knob steps and stops at the ends; OCT+ */
    case 4: {                                       /* wraps; saved */
        uint8_t *v;
        uint32_t m;
        bt_set_get(&set);
        v = btp.sel == 3u ? &set.mode : &set.rate;
        m = btp.sel == 3u ? BT_MODE_LINK : BT_RATE_SLOW;
        k = s > 0 ? (*v < m ? *v + 1u : m) : s < 0 ? (*v ? *v - 1u : 0u) : ok ? (*v < m ? *v + 1u : 0u) : *v;
        if (k != *v) {
            *v = (uint8_t)k;
            bt_set_put(&set);
        }
        break;
    }
    default:
        k = btp.sel - BT_SET_ROWS;
        if (k == n) {                               /* + ADD A DEVICE */
            if (ok && st == BLE_ST_FAILED)
                ui_message("POWER CYCLE TO RETRY");
            else if (ok && !btp.on)
                ui_message("BLUETOOTH IS OFF");
            else if (ok) {
                btp.add = 1;
                btp.asel = btp.an = 0;
                btp.add_t0 = fm1_ms;
            }
        } else if (s < 0) {                         /* a device: a knob left asks FORGET */
            btp.confirm = (uint8_t)(k + 1u);
        } else if (ok) {                            /* OCT+: use it now (around, not linked) */
            int at = bt_devs_state(k);
            if (at == 1)
                bt_devs_use(k);
            else
                ui_message(at ? "CONNECTED" : "NOT AROUND");
        }
        break;
    }
    enc_drop();                                     /* swallow the rest while the page is up */
    return 1;
}

UI_SMALL static uint32_t bt_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* s cut to at most w px (Terminus: 8 px a character), "..." in the last place when it was longer */
UI_SMALL static void bt_fit(char *o, const char *s, int32_t w)
{
    uint32_t n = (uint32_t)(w / 8), i;
    for (i = 0; i < n && s[i]; i++)
        o[i] = s[i];
    o[i] = 0;
    if (s[i] && i)
        o[i - 1u] = '.';
}

/* one line at y (the menu area's rows): the cursor's on a C_LINE bar with white text; its label (and a tag), its value
 * at the right in amber (lit) or dim; dot: 2 a connected device's (amber), 1 a saved one's ring, 0 none */
UI_SMALL static void bt_line(int32_t y, int sel, const char *label, const char *tag, const char *value, int lit, int dot)
{
    char b[32];
    int32_t lx = 8, vw = value[0] ? text_w(&FONT_S, value) : 0;
    if (sel)
        cv_rect(2, y, 236, BT_RH - 1, C_LINE);
    if (dot) {
        te_disc(lx + 3, y + 8, 3, sel ? C_WHITE : dot > 1 ? C_HI : C_DIM);
        if (dot == 1)
            te_disc(lx + 3, y + 8, 1, sel ? C_LINE : C_BLACK);
        lx += 12;
    }
    bt_fit(b, label, (tag ? 142 : 228 - vw - 8) - lx);
    cv_text(lx, y, &FONT_S, b, sel ? C_WHITE : C_GRAY);
    if (tag)
        cv_text(150, y, &FONT_S, tag, sel ? C_WHITE : C_DIM);
    if (value[0])
        cv_text(236 - vw, y, &FONT_S, value, sel ? C_WHITE : lit ? C_HI : C_DIM);
}
UI_SMALL static void bt_row(uint32_t k, const char *label, const char *value)   /* one of the five rows */
{
    bt_line(BT_RY + (int32_t)k * BT_RH, btp.sel == k, label, 0, value, 1, 0);
}

/* the section below the menu's tabs (ui_menu.c draw_menu: band 0 covers y 0..123 of the area, band 1 124..218; cv_oy
 * set by the caller), drawn into whichever band is being drawn */
UI_SMALL static void bt_page_band(void)
{
    char st[40], lb[24];
    const char *err, *keys;
    uint32_t code, v, n;
    int32_t y;
    bt_set_t set;
    bt_dev_t d;
    bt_set_get(&set);
    n = set.role ? 0u : bt_devs_n();
    bt_state_text(st);
    err = ble_ui_state() == BLE_ST_FAILED ? "" : ble_ui_err(&code);
    bt_fit(lb, st, 232);
    cv_text(4, BT_SY, &FONT_S, lb, btp.confirm ? C_WHITE : C_HI);
    if (err[0]) {
        bt_fit(lb, err, 232);
        cv_text(4, BT_EY, &FONT_S, lb, C_DIM);
    }
    if (btp.add) {                                  /* the ADD list over the rows: a device a line, BLE-MIDI tagged */
        for (v = 0; v < btp.an; v++) {
            char r[8];
            const ble_ui_dev_t *a = &btp.adev[v];
            if (a->name[0])
                bt_name(lb, a->name, sizeof lb);
            else
                *bt_hex2(bt_hex2(bt_hex2(lb, a->addr[5]), a->addr[4]), a->addr[3]) = 0;
            fmt_int(r, a->rssi);
            bt_line(BT_RY + (int32_t)v * BT_RH, btp.asel == v, lb, a->midi ? "MIDI" : 0, r, 0, 0);
        }
        if (!btp.an)
            cv_text(4, BT_RY + 60, &FONT_S, fm1_ms - btp.add_t0 < BT_ADD_MS ? "LOOKING FOR DEVICES..." :
                    "OCT+ SCAN AGAIN", C_DIM);
        keys = "OCT+ ADD   OCT- BACK";
    } else {
        bt_row(0, "BLUETOOTH", btp.on ? "ON" : "OFF");
        bt_row(1, "AUTOSTART", set.autostart ? "ON" : "OFF");
        bt_row(2, "ROLE", set.role ? "DEVICE" : "HOST");
        bt_row(3, "MODE", BT_MODE_N[set.mode & 3u]);
        bt_row(4, "RECONNECT", BT_RATE_N[set.rate < 3u ? set.rate : 0u]);
        if (!set.role) {                            /* the saved devices and ADD, BT_VIS lines in view */
            uint32_t lines = n + 1u - btp.top < BT_VIS ? n + 1u - btp.top : BT_VIS;
            cv_rect(4, BT_DY - 3, 232, 1, C_LINE);  /* (the rule over the devices, as SLOOP's under the header) */
            for (v = 0; v < lines; v++) {
                uint32_t k = btp.top + v, ds;
                int sel = btp.sel == BT_SET_ROWS + k;
                y = BT_DY + (int32_t)v * BT_RH;
                if (k < n && bt_devs_get(k, &d)) {
                    ds = (uint32_t)bt_devs_state(k);
                    bt_dev_label(&d, lb);
                    bt_line(y, sel, lb, d.kind == BT_KIND_FM1 ? "FM-1" : "CTRL", bt_dev_value(k), ds != 0u,
                            ds == 2u ? 2 : 1);
                } else {
                    bt_line(y, sel, "+ ADD A DEVICE", 0, "SCAN", btp.on, 0);
                }
            }
        }
        keys = btp.sel >= BT_SET_ROWS && btp.sel - BT_SET_ROWS < n ? "KNOB LEFT FORGET  OCT+ USE" :
               "PRESETS ROW KNOB SET OCT+ OK";
    }
    cv_text(4, BT_HY, &FONT_S, keys, C_DIM);
}

/* the section's signature (ui_menu.c draw_menu: drawn again when it changes): the state line, the last error, the
 * rows, the saved devices' lines and states, the ADD list, the cursor */
UI_SMALL static uint32_t bt_page_sig(void)
{
    char st[40];
    const char *err;
    uint32_t code, sig, v, n, rows;
    bt_set_t set;
    uint8_t ds[BT_NDEV];
    if (btp.add)
        bt_add_list();
    bt_set_get(&set);
    n = set.role ? 0u : bt_devs_n();
    rows = bt_page_rows();
    if (btp.sel >= rows)
        btp.sel = (uint8_t)(rows - 1u);
    if (btp.sel < BT_SET_ROWS)                      /* the device line under the cursor in view */
        btp.top = 0;
    else if (btp.sel - BT_SET_ROWS < btp.top)
        btp.top = (uint8_t)(btp.sel - BT_SET_ROWS);
    else if (btp.sel - BT_SET_ROWS >= btp.top + BT_VIS)
        btp.top = (uint8_t)(btp.sel - BT_SET_ROWS - BT_VIS + 1u);
    for (v = 0; v < n; v++)
        ds[v] = (uint8_t)bt_devs_state(v);
    bt_state_text(st);
    err = ble_ui_state() == BLE_ST_FAILED ? "" : ble_ui_err(&code);
    sig = bt_hash(2166136261u, st, str_len(st));
    sig = bt_hash(sig, err, str_len(err));
    sig = bt_hash(sig, &btp, sizeof btp);
    sig = bt_hash(sig, &set, sizeof set);
    sig = bt_hash(sig, ds, n);
    sig = bt_hash(sig, bts_get()->dev, sizeof bts.dev);   /* (the saved devices' bytes) */
    return (sig ^ n) * 16777619u;
}

/* a track whose SAMPLE set or GRAIN source is a built-in set this build left out (eng_sample.c smp_set_missing): NO
 * SAMPLE on the top bar (SAMPLE plays a sine there, GRAIN nothing). Once per track and source: at power-on, a sound or
 * project load, SET / SRC turned; again after another source. One a frame, never over a message showing. Not on
 * notes: nothing in the audio path (core_ble_api.c bt_service, every pass) */
UI_SMALL static void smp_notice(void)
{
    static uint8_t said[NTRK];
    uint32_t k, code;
    for (k = 0; k < NPART; k++) {
        const track_t *t = &trk[k];
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
        code = (e == &ENG_SAMPLE || e == &ENG_GRAIN) && smp_set_missing(si) ? 1u + si + (e == &ENG_GRAIN ? 32u : 0u) : 0u;
        if (code == said[k])
            continue;
        if (code && ui.msg_t)
            return;                                 /* (after the message showing: LOADED) */
        said[k] = (uint8_t)code;
        if (code) {
            ui_message("NO SAMPLE");
            return;
        }
    }
}
