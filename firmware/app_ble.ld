/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Bluetooth build (FELUCCA_BLE=1, tools/build.py): app.ld (the same memory map; keep the MEMORY block equal to
 * it) plus the JieLi BT libraries' sections, placed as the AC79 SDK's cpu/wl82/sdk_ld_sfc.c does (Apache-2.0,
 * JieLi): their code and constants in XIP (.text), their data in .data, their zeroed data in .bss. The fragments'
 * pool symbols are given with zero size (no classic BT, no TWS). The radio calibration
 * (*.rf.text.cache.L2.rf_trim, .volatile_ram_code) runs from RAM, in .ram_text, as the SDK's demos build it
 * (CONFIG_RF_TRIM_CODE_AT_RAM): during the ~500 ms trim the flash must not be read (ble_port.c's
 * enter/exit_wifi_rf_trim_region turn interrupts off; build.py's reach check: .ram_text reaches nothing in XIP).
 * build.py fails the link on any other *ram_code* section.
 * SLOOP's pool has no room for the BLE unit's heap and task stacks (ble.c ble_heap, ble_os.c tasks: its .pool, 31 KB),
 * its RAM region has: they go to .bss (core_ble_api.c core_ble_stack_window: the stack window from _bss_start), and the
 * BT libraries' and the BLE unit's zeroed data goes out of .bss in their place: what fits to .lowram (the libraries'
 * named *_bss sections), the rest to .pool (the BLE unit's .bss, the libraries' .bd_base). All three are zeroed at
 * boot (main.c). docs/ble/FIT.md has the numbers.
 * BLE MIDI: 2026 DEADACTIVE (from Felucca's ble-kit app_ble.ld; Hortator, fm1-lsdj 548ce73)
 * Felucca memory map (SLOOP's app.ld).
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C07F00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver; here also the radio calibration), .c1_text (the second core's waiting loop,
 * fm1_cpu1.h) and, after them, .lowram (sloop-lab: the reverb lines, the second core's stacks and the render split's
 * scratch). crt0.S writes the SPL's state at 0x01C07F20: the region stops at 0x01C07F00. main.c proves and zeroes
 * .lowram at boot.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
        . = ALIGN(4);
        a2dp_source_media_codec_begin = .; KEEP(*(.a2dp_source_media_codec)) a2dp_source_media_codec_end = .;
        a2dp_sink_media_probe_begin = .; KEEP(*(.a2dp_sink_media_probe)) a2dp_sink_media_probe_end = .;
        a2dp_sink_media_codec_begin = .; KEEP(*(.a2dp_sink_media_codec)) a2dp_sink_media_codec_end = .;
        a2dp_event_handler_begin = .; KEEP(*(.a2dp_event_handler)) a2dp_event_handler_end = .;
        sdp_record_item_begin = .; KEEP(*(.sdp_record_item)) sdp_record_item_end = .;
        bt_sleep_begin = .; KEEP(*(.bt_sleep)) bt_sleep_end = .;
        tws_func_stub_begin = .; KEEP(*(.tws_func_stub)) tws_func_stub_end = .;
        tws_sync_channel_begin = .; KEEP(*(.tws_sync_channel)) tws_sync_channel_end = .;
        _sys_event_handler_begin = .; PROVIDE(sys_event_handler_begin = .); KEEP(*(.sys_event.*))
        _sys_event_handler_end = .; PROVIDE(sys_event_handler_end = .);
        lp_target_begin = .; KEEP(*(.lp_target)) lp_target_end = .;   /* (include_lib/driver/cpu/wl82/system.ld) */
        /* every library *_code section but .volatile_ram_code (in .ram_text below; first match wins) */
        *(.[a-uw-z]*_code) *(.v[a-np-z]*_code) *(.*_const) *(.LOG_TAG_CONST)
        . = ALIGN(4); _etext = .;
    } > XIP
    .ram_text : {
        _rt_start = .; KEEP(*(.ram_text*))
        *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
        . = ALIGN(4);
    } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    _rt_end = _c1_end;                  /* the guard's RAM code window covers both (fm1_cpu1.h) */
    /* the SRAM left below the RAM region: section(".lowram") buffers and the second core's stacks (fm1_cpu1.S
     * .bss.c1stack), outside the guard's RAM code window; zeroed at boot as .pool is (main.c lowram_test) */
    .lowram (NOLOAD) : {
        . = ALIGN(8); _lr_start = .; *(.bss.c1stack) *(.lowram*)
        *(.bt_stack_bss) *(.classic_lmp_bss) *(.ble_hci_bss) *(.classic_rf_bss) *(.ble_ll_bss) *(.ble_att_bss)
        *(.ble_db_bss) *(.ble_gatt_bss) *(.ble_rf_bss) *(.hci_controller_bss) *(.ble_sm_bss)
        . = ALIGN(4); _lr_end = .;
    } > RAMTEXT
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .; *(EXCLUDE_FILE(*ble.o) .bss EXCLUDE_FILE(*ble.o) .bss.*) *(.sbss*) *(EXCLUDE_FILE(*ble.o) COMMON)
        *(.*_bulk) *(.btstack_pool)
        acl_tx_pool = .; acl_tx_pool_end = .; acl_rx_pool = .; acl_rx_pool_end = .;
        tws_bulk_pool = .; tws_bulk_pool_end = .;
        . = ALIGN(8); *ble.o(.pool .pool.*)          /* the BLE heap and task stacks (8-aligned: ble_os.c tasks) */
        . = ALIGN(4); _bss_end = .;
    } > RAM
    .pool (NOLOAD) : {
        _pool_start = .; *(.pool*)
        *ble.o(.bss .bss.* COMMON) *(.bd_base) *(.*_bss)
        . = ALIGN(4); _pool_end = .;
    } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) *(.debug*) }
}
