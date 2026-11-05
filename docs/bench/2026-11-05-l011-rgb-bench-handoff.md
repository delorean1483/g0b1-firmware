# Bench handoff: dev kit + L011 breakout (lamp + heater), 2026-11-05 16:10

For Robb and his Claude Code. The bench scripts live in this repo's `tools/`.
Copies, plus the flashed L011 image, are also on the dev kit under `/data/bench/`.

## 1. Setup on your machine

- Dev kit: `ssh root@192.168.0.94` (empty password). Variscite DART-MX8MM, Cortex `v1.2.86-validate`.
  The G0B1 R0 runs v1.1.7 (Modbus reg 2 = 10107). Your L011 breakout is on its ST1, lamp on HDR2, heater on HDR3.
- **gobi-agent is STOPPED on purpose.** The bench scripts own the RS-485 port (`/dev/ecofleet-rs485`).
  To go back to normal, run `systemctl start gobi-agent`. It restores the saved lamp color 255,208,123 (peach) from `/data/ecofleet/led.json`.
- Scripts that run on the dev kit: `tools/rgb_bench.py`, `tools/bl_flash.py`, `tools/g0b1_reset.py`, `tools/rgb_rapid.py`.
  Copy them to one directory on the unit; they import `bl_flash`. Copies are already in `/data/bench/`; run them from that directory.
- ST-Link V3SET: wired to breakout **HDR1 (L011 SWD)**. Plug its USB into your machine.
  Install STM32CubeProgrammer, then `export STM32_PROG=/path/to/STM32_Programmer_CLI`.
- Host-side SWD scripts, run on your machine with the ST-Link: `tools/l011_watch.py` (dumps the L011 heater registers) and `tools/i2c_recover_swd.sh` (I²C bus recovery, see section 3).
- **Always connect with `-c port=SWD mode=HOTPLUG`.** That reads and writes the running L011 without halting or resetting it.
  The CLI prints "Failed to download data" after a write to a GPIO BSRR register. That's harmless: BSRR is write-only, so the read-back check always fails.

## 2. State at handoff

- **Lamp: off.** Color test passed visually: channel map, polarity, and 0 = dark are all correct. Ramps: blue was seen; red and green were confirmed by holding each one.
- **L011:** flashed with the heater coprocessor firmware (g0b1-firmware main 5d65728). M1 passed: 16 MHz clock, no watchdog resets.
- **Heater type is FORCED to VEVOR** (G0B1 reg 74 = 1). This is saved in the L011's EEPROM.
  The L011 sends a STOP-only poll every 1 s at 4800 baud, so the heater cannot start: run request is 0.
  To go back to auto-detect, write reg 74 = 255 (see section 5).
- Heater: powered, its own panel unplugged, data wire on HDR3. Wiring unchanged all session.

## 3. Open issue A: the I²C2 bus jams, killing lamp + heater until the G0B1 is reset

**Facts (all measured):**
- The G0B1 is the I²C2 master at 100 kHz: TIMINGR 0x60302730, PCLK 64 MHz, SCL = PA11, SDA = PB14. Pull-ups are external; no internal pulls.
  On the bus: IS31FL3293 at 0x6C and the L011 at 0x64.
- The bus jams in two patterns:
  - **SCL held low.** The G0B1 master holds it: with the L011's I²C off and its internal pull-up on, SCL stayed low, and a G0B1 reset freed it.
  - **SDA held low with SCL high.** Not the L011. It was released by a single SCL clock plus a STOP driven from the L011, and the G0B1 then **resumed on its own** without a reset.
- **Repro:** `python3 rgb_rapid.py 0.1 60 lit` (agent stopped) jams or fails within about 1 s of the lamp first lighting from dark. That happened 4 times out of 4 (0.8–1.1 s).
  The same traffic with the lamp dark gave 0 failures in 376 writes. Slow writes can fail too: the 2 s/step ramp jammed at its third step.
- Resetting the L011 by itself did NOT jam the bus (0 of 3).
- The G0B1 firmware has **no I²C bus recovery**. The HAL's 10 ms timeout covers the whole transfer, and the heater poll reads 54 bytes (≈6–7 ms at 100 kHz), so the margin is thin.

**How to tell it's jammed:** G0B1 reg 79 shows the present bit cleared (0x0004 or 0x0000), and reg 67 (heater transport errors) keeps climbing.
Line levels from the L011: `$STM32_PROG -c port=SWD mode=HOTPLUG -r32 0x50000010 4`. Bit 9 is SCL and bit 10 is SDA; both should read 1 when the bus is idle.

**To recover:** `cd /data/bench && python3 g0b1_reset.py`. This writes Modbus reg 34, which resets the G0B1; the agent must be stopped. For an SDA-low jam you can also run `STM32_PROG=... tools/i2c_recover_swd.sh` on your machine (it bit-bangs SCL from the L011, sends a STOP, then resets the L011).

**Next, hardware:**
1. Scope CH1 on SCL and CH2 on SDA (breakout HDR2 pins 3 and 2), probe ground at the breakout.
2. Single-shot trigger on SDA low for more than 1 ms, trigger point at the right edge of the screen.
3. Run the repro and look for spikes or ground bounce at lamp turn-on, near the last transfer before the bus sticks.
4. Also check the 3.3 V at HDR2 pin 1, and the lamp's current return path (the harness has a single GND pin).

**Next, firmware:**
- Add G0B1 I²C2 recovery: on error, toggle PE; if SDA is low, send 9 clocks and a STOP; then re-init.
- Optionally enable the digital filter (DNF) or drop to 50 kHz. A 50 kHz test build was planned (TIMINGR 0xF0102527, timeouts 10 → 20 ms, version 1.1.90) but has **not** been built or flashed.

## 4. Open issue B: heater, the L011 receive input reads LOW, no comms

**Facts:**
- Interface pins are correct for VEVOR 3-wire:
  - PA6 MODE = 0 (3-wire path)
  - PA7 nEN = 0 (TX path connected)
  - PB1 PWR = 0 (+5 V_HTR off)
  - LPUART1 TX inverted (CR2 TXINV); PA1 idles low
- **PA0 RX reads 0 constantly.** A UART line should idle high. As a result, ECHO_ERRS climbs by one per poll, valid frames = 0, CRC fails = 0, LINE_SENSE = 0x00.
- At about 15:28 the same path saw line A idling high (LINE_SENSE 0x11, 3-wire class), with no valid reply. Since about 15:48 it reads low. The cause of that change is unknown.
- Turning on PB1 (+5 V_HTR) by hand did **not** make RX go high (tested, then restored to 0).

**Next, measurements:**
1. **Heater data wire at HDR3 to GND.** Is the idle voltage high? On the scope, there should be a ~33 ms burst every 1 s (our poll), followed by the heater's reply if it answers.
   - Flat at 0 V: the heater isn't holding the line up and our transmit isn't reaching it. Check the heater's 12 V and which HDR3 pin carries the data wire (it should be line A).
   - Bursts visible: transmit works, so the fault is on the receive side (items 3 and 4).
2. **Heater supply:** 12 V at the heater's own connector?
3. **Comparator output into the L011** (PA0 net, TP5 or TP6 by U5). If it stays low while the line idles high, the receive buffer is at fault: comparator supply, reference, or output pull-up.
4. **Comparator reference** (`HTR_RX_REF`, TP7 area): compare it with the data line's idle level.

Watch the L011 live with `STM32_PROG=... python3 l011_watch.py 20 1.5`, one line per read: type, phase, flags, valid, crc, echo, line sense.

**Safety:**
- Do NOT start the heater. RUN_REQUEST comes only from G0B1 reg 53 or the Cortex HeaterCard, which stays hidden while the agent is stopped.
- Bench steps M2 (scope PA6, PA7 and PB1 from power-on) and the `htr_board.h` schematic checklist have not been done yet.
  See `g0b1-firmware/docs/heater-coprocessor.md`.

## 5. Register cheat sheet

**G0B1 Modbus** (slave 1, 9600 8N1, wire address = reg − 1):

| Reg | Meaning |
|---|---|
| 2 | firmware version (10107 = 1.1.7) |
| 34 | any write resets the G0B1 |
| 53 | heater run request (don't) |
| 67 | heater transport errors |
| 68 | heater type |
| 71 | phase (1 = detecting) |
| 73 | (DETECT_RESULT << 8) \| LINE_SENSE |
| 74 | type select: 255 auto, 0 none, 1 VEVOR, 2 AUTOTERM |
| 76–78 | lamp R, G, B (write = request, read = applied) |
| 79 | lamp status: bit 0 present, bit 1 last apply OK, bit 2 pending |

Write reg 74 = 255 (back to auto) from `/data/bench`:

```
python3 -c "from bl_flash import Link,SLAVE_ADDR; l=Link('/dev/ecofleet-rs485'); l._send(bytes([SLAVE_ADDR,6,0,73,0,255])); print(l.ser.read(8).hex()); l.close()"
```

**L011 memory** (build 5d65728):

| Address | What |
|---|---|
| 0x20000369 | heater register file `s_live`, 0x46 bytes (map: `g0b1-firmware/heater-common/htr_i2c_map.h`) |
| 0x20000004 | `s_ms`, uptime in ms |
| 0x20000000 | `SystemCoreClock` |
| 0x50000010 | GPIOA IDR: PA0 RX = bit 0, PA9 SCL = bit 9, PA10 SDA = bit 10 |
| 0x50000414 | GPIOB ODR: PB1 PWR = bit 1 |
| 0x40005400 / 0x40005418 | I2C1 CR1 (0xBF normal) / ISR (BUSY = bit 15) |
| 0x40021050 | RCC_CSR (IWDGRSTF = bit 29) |

**Bench docs** (g0b1-firmware repo): `docs/external-rgb.md` (lamp), `docs/heater-coprocessor.md` (M1–M8).

## 6. L011 firmware: source (this repo) + flashed image (`/data/bench/l011/`)

The source is in this repo: `heater-coproc/` and `heater-common/` (the chip runs main 5d65728).
The dev kit also has the flashed build and a standalone source archive; copy them with `scp -r root@192.168.0.94:/data/bench/l011 .`

- `image/heater-l011.{hex,bin,elf,map}`: the exact build that's on the chip now (g0b1-firmware main 5d65728).
  Checksums: hex sha256 bb661bea…, bin sha256 f7cf4ef3…. Flash 10512 / 16384, RAM 1472 / 2048.
- `l011-source-5d65728.tar.gz`: a standalone source tree. It contains `heater-coproc/` (port/l011 + core), `heater-common/`, the CMSIS core headers, `docs/heater-coprocessor.md` and the design spec.
  Verified: building from this archive gives a byte-identical .bin.

**Build** (needs `arm-none-eabi-gcc`, e.g. from STM32CubeIDE or the Arm GNU toolchain):

```
make -C heater-coproc/port/l011            # release (from the repo root)
make -C heater-coproc/port/l011 DEBUG=1    # IWDG frozen while halted (for a debugger)
```

The output goes to `heater-coproc/port/l011/build/heater-l011.{elf,bin,hex}`.

**Board pins live in ONE file:** `heater-coproc/port/l011/htr_board.h`. The checklist in `docs/heater-coprocessor.md` ("Before the first power-up") lists what to confirm against the finished schematic:
- UART pins and inversion
- PA6 MODE, PA7 nEN, PB1 PWR active levels
- HB_PWR_FOR_3W
- I2C1 on PA9/PA10

Fix the header there if the schematic disagrees, then rebuild.

**Flash** (ST-Link on HDR1; the board keeps power from the G0B1):

```
$STM32_PROG -c port=SWD mode=UR -w heater-l011.hex -v -rst
```

After a reflash:
- Re-check the RAM addresses in section 5 with `arm-none-eabi-nm -S build/heater-l011.elf | grep -E "s_live|s_ms|SystemCoreClock"` if the code changed.
- The heater type in EEPROM survives a reflash (it's still forced to VEVOR).
- A G0B1 reset may be needed if the bus jams during the L011 reboot.
