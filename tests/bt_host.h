/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The Bluetooth build's core side (FELUCCA_BLE=1) on SLOOP's UI harness (tests/ui_pages_test.c: the real UI, the
 * sequencer and the audio between frames), the BLE unit's entry points the stubs of tests/ble_ui_stub.h. Each test
 * defines ble_midi_send / ble_midi_room (and BLE_FML_SEND_OWN with its own ble_fml_send) before including this. */
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#define UI_PAGES_HARNESS_ONLY 1
#include "ui_pages_test.c"
#include "ble_ui_stub.h"
static uint32_t bt_test_heard;                      /* (bt_fml.c: the tick the block is heard at, the test's) */
#define BT_FML_HEARD bt_test_heard
#include "../firmware/src/bt_store.c"
#include "../firmware/src/bt_set.c"
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"
/* a power-on of the core's Bluetooth side: an empty Bluetooth record, nothing linked, the UI on HOME */
static void bt_power_on(void)
{
    memset(&bts, 0, sizeof bts);
    bts_loaded = 1;
    bts_dirty = 0;
    memset(&bs, 0, sizeof bs);
    memset(&bh, 0, sizeof bh);
    memset(&bt_dv, 0, sizeof bt_dv);
    memset(&btp, 0, sizeof btp);
    memset(&bt_auto, 0, sizeof bt_auto);
    memset(&bt_in, 0, sizeof bt_in);
    memset(&smc, 0, sizeof smc);
    bt_out_r = bt_out_w = 0;
    conn_up = 0;
    song.solo = 0;
    ui.menu = 0;
    ui.msg_t = 0;
    fm1_ms = 100000u;
}
/* the UI's start as tests/ui_pages_test.c's: the default panel, the tracks' default sounds, HOME */
static void bt_ui_init(void)
{
    uint32_t i;
    panel = PANEL_DEFAULT;
    layers_init();
    settings.palette = 4;
    palette_set(4);
    host_tracks_init();
    for (i = 0; i < NPART; i++) {
        set_engine_of(&trk[i], TRK_DEF[i][0]);
        apply_preset_to(&trk[i], TRK_DEF[i][1]);
        trk[i].engine = trk[i].eng_req;
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.playing = 0;
    song.rec = 0;
    song.sel = 0;
    mi_r = mi_w = 0;
    transport_req = 0;
    go_home();
    ui.force = 1;
    frame();
}
