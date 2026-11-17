# L011 I²C update bench procedure

The initial SWD image contains the resident loader at `0x08000000`,
the map-v2 heater application at `0x08001000`, and its validity record
at `0x08003F80`. The separate app binary is the only input to the Cortex
updater. Build from the L011 loader and app source checkout:

```sh
make -C heater-coproc/port/l011 LAYOUT=loader
make -C heater-coproc/port/l011 LAYOUT=bootloader
python3 tools/l011_package.py \
  heater-coproc/port/l011/build-loader/heater-l011.bin \
  heater-coproc/port/l011/build-relocated/heater-l011.bin l011-combined-swd.hex
```

## First bench installation

Keep the heater outputs disconnected. Save read-only snapshots of the L011
program flash, data EEPROM, and option bytes. Program and verify only the
program-flash pages in the combined HEX using SWD and page erase. Leave
mass erase, read-protection changes, and option-byte programming disabled.
Compare EEPROM and option bytes to the snapshots after programming.

Pair with G0B1 1.3.15 or the original 1.3.7 bridge build. Retain the current G0B1 rollback slot.
Verify map v2, heater off, and lamp off before attempting an I²C update.
Keep a matching relocated map-v2 app binary available for I²C rollback.
Restoring a legacy standalone L011 image requires SWD and removes the
resident loader.

## Cortex update

Copy `l011_flash.py`, `bl_flash.py`, and the relocated app binary to Cortex.
With the heater mode off, the lamp off and verified, and the agent stopped
for exclusive serial access, confirm zero run request and L011 phase OFF
(G0B1 holding register 71). For a connected but silent heater, save heater
type select (holding register 74), then write **reg 74 = 0** before the
updater sends **reg 2132 = 0xB011**. Wait for the type write to take effect
and for phase OFF. For example, save 255 if the heater was in auto mode.
Run:

```sh
python3 l011_flash.py l011-app.bin --port /dev/ecofleet-rs485
```

After the script successfully exits maintenance with `0xB012`, restore the
saved register 74 value if you changed it, then restart the agent. If
the loader is still active, leave register 74 at 0 and recover the update
first. The identity status is `0x200` at holding register 2133
(zero-based Modbus address 2132) after the updater completes.

In maintenance the G0B1 pauses heater and lamp polling, rejects writes to
the heater request and command registers and the lamp registers, and
forgets the L011 on entry and exit. Exit requires the app identity and
map version at `0x64`.

The bridge stages one frame at a time in RAM. The loader checks frame CRC16,
erases and verifies each 128-byte page, and checks whole-image CRC32 and the
reset vectors before publishing validity. Flash writes are restricted to
the app and validity pages. Option-byte programming remains locked.

After an interruption, run the same command again. The updater starts at
page zero. There is one L011 app slot, so an interrupted update can leave the
heater app unavailable until a complete image is re-sent. G0B1 only exits
maintenance after the heater app responds with the expected identity and
map. The CRCs detect corruption on the trusted bench link. They do not
authenticate the image.

## G0B1 diagnostic registers (1.3.16 and newer)

These are read-only holding register numbers. Subtract 1 for the Modbus
wire address. Registers 2227 and 2228 contain the identified L011 firmware
version: 2227 packs major in the high byte and minor in the low byte,
and 2228 contains the build. Both read zero while the L011 is unidentified.

Register 2229 records the last `0xB011` entry refusal. Successful entry
clears it. The values are:

| Code | Reason |
| --- | --- |
| 0 | No refusal recorded |
| 1 | L011 phase is not OFF |
| 2 | G0B1 or L011 run request is set |
| 3 | Lamp is on, requested on, or its off state has not been verified |
| 4 | L011 app identity or map version does not match |
| 5 | Heater mode is enabled |
| 6 | Loader or I²C communication failed |

## Physical acceptance test

With outputs isolated, interrupt power during an app page write, reconnect,
confirm the loader answers at `0x65`, then repeat the update from Cortex
with SWD disconnected. Verify the app returns at `0x64`, reports map v2,
and the heater remains off. Reconnect SWD for read-only EEPROM and option
byte comparison. Repeat around validity-record programming before relying
on the I²C path for recovery.
