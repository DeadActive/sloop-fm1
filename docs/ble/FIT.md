# The Bluetooth build on SLOOP: does it fit?

BLE MIDI: 2026 DEADACTIVE. SLOOP 2.8 with Felucca's BLE unit (`FELUCCA_BLE=1 sh ./build.sh`), measured against the app
slot (`APP_SLOT` 581,564 B). `FELUCCA_BLE=1 BLE_MEASURE=1 sh ./build.sh` measures without enforcing the budgets.

## Flash

| | image B | slot free B |
| --- | ---: | ---: |
| SLOOP 2.8, plain | 567,308 | 14,256 |
| + the BLE stack (the unit, the libraries, the radio calibration's RAM code), BASS VIBES HORNS FLUTE left out | 572,092 | 9,472 |
| + the core's side (the record, the page, MIDI, FM-Link, the SMC layout, the icon, the sine for the sets left out) | 584,468 | -2,904 |
| **+ the boot logo as text (SLOOP's own fallback, `FELUCCA_LOGO_TEXT=1`): the build as it is** | **580,068** | **1,496** |

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

All of it is static (nothing grows at run time; the stacks are elsewhere), but the budgets every Felucca port keeps
(4 KB of `.data` + `.bss`, 8 KB of pool spare) cannot both hold: over the RAM region, the pool and the low RAM 37,360 B
are left for the BLE unit's 31.0 KB and its 13.4 KB less what it can move; 6,368 B remain where the two budgets want
12,288 B. **Open question for the user** (the build keeps the pool budget at 8 KB and stops on it until it is decided;
`BLE_POOL_FLOOR=N` sets another):

1. Accept smaller budgets in the Bluetooth build only: RAM 5.2 KB spare, pool 852 B (or any split of the 6.4 KB).
2. Shrink one of SLOOP's buffers in the Bluetooth build by at least 6 KB (e.g. the delay line, 128 KB for 1.49 s).
3. One core in the Bluetooth build (frees the second core's stacks in `.lowram`): excluded by the user's decision.
4. A smaller BLE heap: a change to the BLE unit (layer 1), against the measured 17.9 KB high with two links.
