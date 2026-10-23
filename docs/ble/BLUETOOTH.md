# Bluetooth MIDI on SLOOP (the Bluetooth build)

BLE MIDI: 2026 DEADACTIVE. Ported from Felucca's reference (DeadActive/Felucca, branch `ble-kit`, docs/ble/PORTING.md)
onto SLOOP 2.8.

`FELUCCA_BLE=1 sh ./build.sh` builds SLOOP with Bluetooth LE MIDI: **HOST**, the FM-1 keeps up to 8 saved devices and
connects to them by itself, two at once (a BLE-MIDI controller such as an M-VAVE SMC-Mixer, and another FM-1 for
FM-Link); **DEVICE**, the FM-1 is a BLE-MIDI device a computer, a tablet or another FM-1 connects to. The package is
`build/felucca-ble.fwsc`. The plain build (`sh ./build.sh`) is SLOOP 2.8, byte for byte; either installs over the other.

**Not yet run on a device.** The BLE unit is Felucca's, unchanged; its place in SLOOP is checked by the build's gates
and the host tests only.

## What the Bluetooth build gives up

- **Built-in samples:** the SAMPLE sets BASS, VIBES, HORNS and FLUTE (130 KB; the user's choice). PIANO, STRGS, SCRCH
  and the drum kit PERC stay. A sound on a set left out plays a sine at the note's pitch (SAMPLE; GRAIN plays nothing)
  and the top bar says NO SAMPLE once. Every set keeps its number and name, so projects and presets point at the same
  sets as in the plain build. A new project's track 3 is LOFI KEYS (on PIANO) in the Bluetooth build, not LOFI FLUTE
  (the user's choice, 2026-10-23): the default project uses no set left out.
- **The boot logo** is drawn as text (SLOOP's own fallback, `FELUCCA_LOGO_TEXT`): the flash is full otherwise.
- Both cores stay.

## The BT section (HOME held, then SELECT to the last tab)

The menu has a sixth section, **BT** (SYSTEM's tab reads SYS to make room). It shows a state line (and the last error),
the rows **BLUETOOTH**, **AUTOSTART**, **ROLE** (HOST / DEVICE), **MODE** (AUTO, SMC DAW, MIDI, FM-LINK) and
**RECONNECT** (AUTO, FAST, SLOW), then (ROLE HOST) the saved devices, three in view, and **+ ADD A DEVICE**.

SLOOP's other sections set row *k* with KNOB *k*; this one has more rows than knobs, so here **PRESETS** moves the
cursor and **any of KNOB 1-4** changes the cursor's row (right / left), **OCT+** acts on it (toggles, steps round,
uses a device, adds one). **SELECT** goes back to SYSTEM, **OCT-** closes the menu (from the ADD list or a FORGET
question: back / no).

| Row | A knob | OCT+ |
| --- | --- | --- |
| BLUETOOTH | right ON, left OFF | ON / OFF |
| AUTOSTART | right ON, left OFF (saved) | ON / OFF |
| ROLE | right DEVICE, left HOST (saved) | HOST / DEVICE |
| MODE | AUTO, SMC DAW, MIDI, FM-LINK (stops at the ends; saved) | the next (wraps) |
| RECONNECT | AUTO, FAST, SLOW (stops at the ends; saved) | the next (wraps) |
| a saved device | left: `FORGET <name>?` on the state line, OCT+ yes, OCT- no | NEAR: use it now |
| + ADD A DEVICE | - | an 8 s scan for nearby devices not saved, then the list stays |

Each device line: a dot (connected) or a ring, its name (an FM-1 as `FM-1` and the last four hex digits of its address),
`CTRL` or `FM-1`, and ON / NEAR / AWAY. The rest is Felucca's behaviour (its docs/ble/BLUETOOTH.md): Bluetooth is off
at every power-on unless AUTOSTART is ON (then it turns on about 3 s after the start, after a clean boot only: not after
a crash, a watchdog reset or a failed boot, `BT AUTOSTART SKIPPED`); after FAILED only a power cycle starts it again
(`POWER CYCLE TO RETRY`); two links at once, one controller and one FM-1, reconnecting by themselves with a 30 s grace.

The top bar shows the Bluetooth rune (lit while a link is up, struck through while Bluetooth is on and nothing is
linked, nothing when off): on the live screens after the beat lights, on the pages between the track's number and USB.

## MIDI, the clock, FM-Link

- **MIDI mode:** the device is a third MIDI input beside USB and TRS (notes by channel, bend, CCs, Program Change, MIDI
  LEARN), and the notes the FM-1 sends to USB MIDI out (the keys; the sequencer's with GLO > SYSTEM > MIDI = SEQ) go to
  the device too.
- **GLO > SYSTEM > SYNC** has two more values: **BLE** (the device's clock and START / STOP run the transport) and
  **LINK** (FM-Link's follower). The settings word keeps INT for them (the plain build reads INT); the Bluetooth record
  keeps the value.
- **Clock out:** on SYNC INT, linked in MIDI mode, the FM-1 sends Start, 24 pulses a beat and Stop to the device.
- **FM-Link:** two FM-1s in time without MIDI clock: the leader on SYNC INT, the follower on SYNC LINK (Felucca's
  engine, unchanged: SLOOP and Felucca keep time with each other).
- **SMC DAW:** faders 1-4 the tracks' LEVEL (the drum track's: GLO > DRUMS LVL), 5-8 their REV send (drums: DRUMS
  REV), with pickup; knobs 1-4 the selected track's HOME parameters; knobs 5-8 MIDI LEARN's; MUTE, SOLO (SLOOP's solo,
  as GLO + a key), REC (the track, then REC), SELECT; PLAY, STOP, REC; BANK / CHANNEL the previous / next track.

## Where it is kept

The Bluetooth settings, this FM-1's address, the saved devices and a copy of the radio calibration (72 bytes, the stock
firmware's config store item, which SLOOP's USR4 sample slot can overwrite: the BLE unit then uses the copy) live in a
300-byte record of their own in each copy of the settings object's flash sector, past its payload
(`firmware/src/bt_store.c`). The settings object itself is exactly what the plain build writes, so SLOOP 2.8 reads it
as before. A save by the plain build or by an older SLOOP leaves the record to the other copy; a second one loses it
(the Bluetooth settings return to their defaults).

## The console

A developer build with the serial console (`FELUCCA_CDC=1`) has `ble` and `fml`, as Felucca. The FM-1 build has no
console.
