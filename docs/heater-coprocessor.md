# Heater coprocessor (EF-L011) — build, flash, bench bring-up

Design: `docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md`.
I²C contract: `heater-common/htr_i2c_map.h` (slave 0x64, map version 2).

## Layout

| Path | What |
|---|---|
| `heater-common/` | shared portable code (I²C map, types, VEVOR + AUTOTERM protocols) |
| `heater-coproc/core/` | portable coprocessor core (drivers, FSM + auto-detect, iface, register file) |
| `heater-coproc/port/l011/` | EF-L011 port: `htr_board.h` (the only schematic-derived file), port_*.c, `main.c`, `Makefile`, `l011.ld`, vendored ST CMSIS device files |
| `App/services/heater.c`, `mbp_heater.c` | G0B1 client + Modbus regs 53–74 |

## Build

```bash
export PATH="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin:$PATH"
make -C heater-coproc/port/l011            # release
make -C heater-coproc/port/l011 DEBUG=1    # freezes IWDG while halted
```

Output: `heater-coproc/port/l011/build/heater-l011.{elf,bin,hex}` and a size line
`flash N / 16384, ram N / 2048` (fails the build above the hard limits, warns above 12 KB).

## Flash / debug (ST-LINK on HDR1)

- CLI: `STM32_Programmer_CLI -c port=SWD mode=UR -w heater-coproc/port/l011/build/heater-l011.hex -v -rst`
- CubeIDE: File ▸ Import ▸ C/C++ ▸ "STM32 Cortex-M Executable", pick `build/heater-l011.elf`, MCU `STM32L011F4Ux`, then Debug.
  Build with `DEBUG=1` so the IWDG stops while halted.

## Before the first power-up: confirm `htr_board.h` against the finished schematic

Every value in `heater-coproc/port/l011/htr_board.h` was read from the R0.00.00 PDF.
Tick each against the **finished** schematic (and the STM32L011x4 datasheet for AF numbers) and fix the header before M2:

- [ ] 3-wire side = LPUART1 (PA0 RX / PA1 TX, AF6); TX inverted, RX not
- [ ] 4-wire side = USART2 (PB6 TX / PB7 RX, AF0); no inversion
- [ ] HTR_TX_MODE (PA6): level that selects the **4-wire** path
- [ ] HTR_TX_nEN (PA7): level that **connects** the TX path (expected low)
- [ ] HTR_TX_PWR / HTR_5V_EN (PB1): level that turns the TX supply **on** (expected high)
- [ ] `HB_PWR_FOR_3W`: does the 3-wire path need +5V_HTR? (expected no)
- [ ] GPIOX6 / GPIOX7 (PA4/PA5): role; leave `HB_GPIOX_DEBUG 0` unless unloaded test points
- [ ] I²C1 PA9 SCL / PA10 SDA, AF1

## Bench milestones (each gates the next)

**M1 — boot.** Flash; in the debugger confirm `SystemCoreClock == 16000000` and that `port_now()` advances.
Let it run 60 s un-halted (release build): no IWDG reset loop (no NRST pulses on a scope).

**M2 — safe GPIO.** Scope PA6, PA7, PB1 from power-on: each must sit at its *inactive* level the whole time
(mux disconnected, +5V_HTR off), with no glitch during reset. With nothing on HDR3 the FSM stays in
DETECTING and never enables the iface — PB1 stays off. Then force a type to watch the enable sequence:
write `0x02` to reg 0x10 (see M3) and confirm order `nEN off → MODE → PWR on → (5 ms) → nEN on`.

**M3 — I²C.** From the G0B1 (Plan 3 build) watch Modbus regs 68–74 / `heater_get_telemetry()`:
`present == 1`. Or from a G0B1 GDB session: `HAL_I2C_Mem_Read(&hi2c2, 0x64<<1, 0x00, 1, buf, 2, 10)` → `EF 01`.
Confirm the IS31FL3293 still ACKs at 0x6C on the same bus.

**M4 — UARTs, no heater.** Loop an FT232 onto each side in turn:
- 3-wire: force type 1 (VEVOR); the FT232 (RX non-inverted) must see a 16-byte STOP poll (`AA 66 06 0B … 05 …`) once a second at 4800 8N1.
  `ECHO_ERRS` (reg 0x3E) must stay 0 — every byte we send comes back through the comparator and is stripped. A climbing `ECHO_ERRS` = TX path / inversion wrong.
- 4-wire: force type 2 (AUTOTERM); the FT232 must see `AA 03 00 00 1C 95 3D` ×35 at 2400, then `… 06 5E BC` ×5 at 4800, then `… 06 5E BC` at 9600.

**M5 — line sense.** Set reg 0x10 = 0xFF (AUTO), command 5 (AUTO_DETECT) with: nothing connected, the VEVOR, the AUTOTERM.
Record reg 0x44 LINE_SENSE each time. Expected: none → class 0; VEVOR → bit0 + class 1; AUTOTERM → bit1 + class 2.
**Record the AUTOTERM result while the heater is asleep** (spec open item 4) — if its TX does not idle high, auto-detect cannot see it and it must be forced.

**M6 — VEVOR live.** Auto-detect → `ACTIVE_TYPE 1`, `SAFE_OFF` set. Fuelled: heartbeat (0x17, a new value at least every 10 s), RUN_REQUEST 0 (arms), then RUN_REQUEST 0xA5, level 1 → STARTING → RUNNING; step levels; RUN_REQUEST 0 → STOP_REQUESTED → COOLDOWN → OFF + SAFE_OFF. Do not cut 12 V until SAFE_OFF.

**M7 — AUTOTERM live.** Auto-detect → `ACTIVE_TYPE 2`. Start at a setpoint; **write down the steady full-power VENDOR_STATE/VENDOR_SUB** (spec open item 5; currently assumed 2.4+/3.x = RUNNING). Stop → cooldown (state 4.x) → 0.1 standby. Note whether the heater needs `0x04`/`0x0B` (it should not; the driver never sends them).

**M8 — full chain.** Cortex HeaterCard → Modbus → G0B1 → coprocessor → heater start/stop. Then, while running, disconnect the G0B1 I²C (or halt the G0B1): after `HOST_TIMEOUT_S` (default 10 s) the coprocessor must run a normal stop + cooldown and set `HOST_TIMEOUT_STOP` (FLAGS bit 5). When the G0B1 comes back it must NOT restart the heater: it reads the flag, drops its request, and needs a fresh ON.

## Run control (map v2)

- **Only complete writes apply.** A write is applied at its STOP. One cut by a bus error or a repeated START, or one that touches anything outside 0x10–0x18, is dropped whole and counted in `WRITES_DISCARDED` (0x46).
- **Heartbeat.** The host is live while it writes a *changed* value to 0x17 at least every `HOST_TIMEOUT_S` (3–60 s, default 10, RAM only). Reads don't count, and neither does rewriting the same value.
- **Keyed start.** `RUN_REQUEST = 0xA5` starts only while the host is live, the start is armed, and the heater is idle (PHASE 0 off). `RUN_REQUEST = 0` (or COMMAND 2 STOP) is a clean stop: always accepted, and it arms the next start. An accepted start, a refused start, a host timeout and a FAULT disarm. Any other RUN_REQUEST value is BAD_ARG and stops without arming. COMMAND 1 START is NOT_SUPPORTED.
- **Refused starts** set CMD_RESULT 1 BUSY (stopping, cooling down, detecting or FAULT; from fw 0.2.2), 5 NOT_ARMED or 6 NO_HOST (4 NO_TYPE as before), FLAGS b11 `START_REFUSED`, and disarm. A start is never held: one accepted during cooldown used to restart the heater minutes later at standby. Disarming is what stops the G0B1 from re-sending its key: it drops its request whenever START_ARMED is clear (the START_REFUSED edge alone misses a second refusal in a row), and its next RUN 0 re-arms for the user's next ON.
- **`HOST_TIMEOUT_STOP`** (FLAGS b5) is set when a host timeout stops a running request and, from fw 0.2.2, stays set until the next accepted start (a clean stop no longer clears it), so a timeout stop is visible afterwards.
- **Coprocessor reset with the heater running:** after the 3 s boot quiet the driver polls STOP; a heater reporting STARTING/RUNNING with no request goes to STOP_REQUESTED, and one reporting COOLDOWN is tracked as COOLDOWN (fw 0.2.2). Either way a fresh keyed start is needed, and it is refused until standby.
- **EEPROM.** Only the heater type is persisted. The old minutes byte is written as 10 so a rolled-back v1 image still has its 10-minute timeout.

## Register cheat sheet (full map: `heater-common/htr_i2c_map.h`)

| Reg | Name | Notes |
|---|---|---|
| 0x00/0x01 | DEVICE_ID / MAP_VERSION | `EF 02` |
| 0x10 | HEATER_TYPE | FF auto, 0 none, 1 VEVOR, 2 AUTOTERM (persisted) |
| 0x11–0x15 | RUN_REQUEST, LEVEL, SETPOINT_C, AMBIENT_C_X10 | run: 0 stop, 0xA5 run. G0B1 writes 0x11–0x17 every 250 ms |
| 0x16 | HOST_TIMEOUT_S | 3–60 s, default 10, RAM only (no off) |
| 0x17 | HOST_HEARTBEAT | changed value = host live |
| 0x18 / 0x19 | COMMAND / CMD_RESULT | 2 stop 3 clear-fault 4 reinit 5 auto-detect (1 start: not supported). Results 0 ok 1 busy 2 bad-arg 3 not-supported 4 no-type 5 not-armed 6 no-host |
| 0x20 | PHASE | 0 off 1 detecting 2 starting 3 running 4 stop-req 5 cooldown 6 fault |
| 0x22 | FLAGS | b0 present b1 fresh b2 safe-off b3 comms-fault b4 iface b5 host-timeout b6 echo-fault b7 detect-done b8 fault-latched b9 host-live b10 start-armed b11 start-refused |
| 0x26/0x27 | VENDOR_STATE / SUB | raw (AUTOTERM major.minor) |
| 0x3E | ECHO_ERRS | 3-wire TX-path health |
| 0x42 | HOST_IDLE_S | seconds since the last valid heartbeat |
| 0x44 / 0x45 | LINE_SENSE / DETECT_RESULT | auto-detect diagnostics |
| 0x46 | WRITES_DISCARDED | u16, dropped partial or out-of-block writes |

## AUTOTERM selectable mode (L011 0.2.4, G0B1 1.3.17)

Map v2 is retained. The extension uses previously unused I2C byte `0x1A`;
existing control/status offsets and legacy transactions are unchanged.
`0x19` remains read-only and cannot be included in a committed control write.
Write mode as its own one-byte transaction.

| Interface | Register / bit | Meaning |
|---|---|---|
| L011 I2C | `0x1A`, RW, persisted | 0 = temperature, 1 = power |
| L011 CAPS | `0x40..0x41`, bit 8 (`0x0100`, `HTR_CAP_POWER_MODE`) | Set only for the active AUTOTERM driver |
| G0B1 Modbus | holding **2230**, wire address 2229 | Applied mode, 0 = temperature, 1 = power |
| G0B1 Modbus | holding 70, bit 8 | Same capability bit, for agent/UI support detection |
| G0B1 Modbus | holding 54 | Stored level 1..10; used by AUTOTERM in power mode |
| G0B1 Modbus | holding 69 | Stored setpoint 5..30 C; used in temperature mode |

Both level and setpoint remain writable in either mode. A mode write is
accepted asynchronously; 2230 continues to show the previous **applied** value
until a fresh L011 poll confirms it. Unsupported/unidentified devices read 0;
mode writes on unsupported devices return Modbus ILLEGAL_VALUE (including 0).
Maintenance also refuses mode writes. On L011, values above 1 leave the register
unchanged and set BAD_ARG. Power on an active VEVOR returns NOT_SUPPORTED and
its mode reads 0. VEVOR wire behavior is unchanged.

Temperature START payload: `FF FF 02 <setpoint> 01 FF 11`.
Temperature SETTINGS payload: `FF FF 02 <setpoint> 01 FF`.
Power START payload: `FF FF 04 <setpoint> 01 <level-1> 11`.
Power SETTINGS payload: `FF FF 04 <setpoint> 01 <level-1>`.
Level 1 maps to 00, level 10 to 09. Wait stays 01 and the START suffix stays 11.
Temperature reports (command 11, signed whole C) remain every 10 seconds in
**both** modes, exactly as in the 0.2.90 bench experiment.

An active-mode level/setpoint change or any mode change queues one SETTINGS
write, with no restart and no new keyed RUN_REQUEST. Inactive values are stored
without a settings write and used on switching mode. STOP and silence-stop take
priority. Existing REQUEST_AGAIN, keyed-start, cooldown and 5-second Cortex
silence behavior remain unchanged.

EEPROM compatibility: the legacy word at 0x08080000 (type, legacy timeout,
checksum) is unchanged. A separate word at +4 contains 0x4D4F in the upper half,
mode in byte 0 and its complement in byte 1. Missing/blank/corrupt/out-of-range
extensions load temperature mode. 0.2.3 ignores the extension on rollback and
still sees its original type/timeout word. The v2 RAM heartbeat timeout remains
10 seconds and is not migrated into EEPROM. Re-upgrading preserves a valid
extension written by 0.2.4; the older image never edits it.

Compatibility checks cover new host/old L011 (no extension writes), old host/new
L011 (temperature default and unchanged old control block), and new/new mode
pass-through. No firmware is flashed as part of this change.

### Hardware validation still required

The 0.2.90 experiment supported the tested source 04/power-byte combination,
but the precise power scale/minimum, wait meaning and trailing 11 meaning are
not established. Hold levels 10 -> 7 -> 4 -> 1 for 8-10 minutes each in a
separately authorized bench run. Switching mode while running is implemented
and mocked but **not hardware-verified**. No release/merge or powered run is
implied by these builds.
When rolling back only the G0B1 while retaining L011 0.2.4, set mode to 0 first if temperature behavior is required. A valid persisted power selection otherwise remains in effect; an older host cannot change it. The old-host compatibility test uses a migrated/default temperature record.
