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
- **Keyed start.** `RUN_REQUEST = 0xA5` starts only while the host is live and the start is armed. `RUN_REQUEST = 0` (or COMMAND 2 STOP) is a clean stop: always accepted, and it arms the next start. An accepted start, a host timeout and a FAULT disarm. Any other RUN_REQUEST value is BAD_ARG and stops without arming. COMMAND 1 START is NOT_SUPPORTED.
- **Refused starts** set CMD_RESULT 5 NOT_ARMED or 6 NO_HOST (4 NO_TYPE as before) and FLAGS b11 `START_REFUSED`. The G0B1 latches its request off on `START_REFUSED`, `HOST_TIMEOUT_STOP` or FAULT, so replaying its old block can't restart the heater.
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
