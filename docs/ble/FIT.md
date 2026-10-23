# The Bluetooth build on SLOOP: does it fit?

BLE MIDI: 2026 DEADACTIVE. SLOOP 2.8 with Felucca's BLE unit (`FELUCCA_BLE=1 sh ./build.sh`), measured against the app
slot (`APP_SLOT` 581,564 B). `FELUCCA_BLE=1 BLE_MEASURE=1 sh ./build.sh` measures without enforcing the budgets.

## Flash

| | image B | slot free B |
| --- | ---: | ---: |
| SLOOP 2.8, plain | 567,308 | 14,256 |
| + the BLE stack (the unit, the libraries, the radio calibration's RAM code), BASS VIBES HORNS FLUTE left out | 572,092 | 9,472 |
| + the core's side (the record, the page, MIDI, FM-Link, the SMC layout, the icon, the sine for the sets left out) | 584,468 | -2,904 |
| **+ the boot logo as text (SLOOP's own fallback, `FELUCCA_LOGO_TEXT=1`): the build as it is** | **580,212** | **1,352** |

The four sets left out are 130,100 B of ADPCM (the user's choice, 2026-10-23: PERC, PIANO, STRGS, SCRCH stay). The
core's side costs about 12.4 KB here (Felucca: about 12 KB with the device role), built for size (`UI_SMALL`, as
SLOOP's own UI). The build retries with the logo as text when the link overflows the slot, as the plain build would.

## RAM

SLOOP's RAM is laid out differently from Felucca's: its `.pool` (big buffers: the delay line, the punch-in ring, the
canvas, the slicer, the engines' arena) has 10,164 B spare in the plain build, its RAM region (`.data` + `.bss`)
39,300 B, the SRAM below it (`.lowram`) 6,716 B (2,740 B once the radio calibration's 4 KB of RAM code is in). The BLE
unit needs 13.4 KB of `.data` + `.bss` (its own and the libraries') and 30,992 B of `.pool` (its heap, 23,552 B, and
three task stacks), which cannot be split (one section of `ble.o`).

| | RAM .data+.bss (of 98,304) | spare | pool (of 344,064) | spare | lowram |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLOOP 2.8, plain | 59,004 | 39,300 | 333,900 | 10,164 | 22,772 of 29,488 |
| the Bluetooth build, the reference's layout (all in the pool) | 73,460 | 24,844 | 365,280 | -21,216 | 22,772 of 25,512 |
| **the Bluetooth build as it is** (app_ble.ld: heap and stacks in .bss; the libraries' zeroed data in .lowram and .pool) | **93,144** | **5,160** | **343,212** | **852** | **25,160 of 25,512** |

All of it is static allocation: nothing grows at run time (the core has no heap; the stacks are elsewhere), so the spare
bytes are a margin for later changes, not for the running FM-1. The budgets every Felucca port keeps (4 KB of `.data`
+ `.bss`, 8 KB of pool spare) cannot both hold here: 6,368 B remain over the RAM region, the pool and the low RAM where
the two want 12,288 B.

**The user's decision (2026-10-23): smaller margins in the Bluetooth build only.** `tools/build.py` keeps
`BLE_RAM_FLOOR` = 4,096 B of `.data` + `.bss` (it holds: 5,160 B spare) and `BLE_POOL_FLOOR` = 512 B of pool (852 B
spare, rounded down). The plain build's budgets are unchanged (8 KB of pool). The alternatives were: shrinking one of
SLOOP's buffers by 6 KB in the Bluetooth build (the delay line), one core (about 3.6 KB, not enough alone, and against
the user's decision to keep both), or a smaller BLE heap (a change to the BLE unit, against Felucca's measured 17.9 KB
with two links).
