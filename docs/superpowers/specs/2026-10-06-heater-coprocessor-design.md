# Heater Coprocessor (EF-L011 → STM32C011) — Design

**Date:** 2026-10-06
**Status:** Draft for review
**Hardware:** EcoFleet L011 Heater/RGB Adapter, EF-L011 R0.00.00 (schematic `L011 Module.pdf`)
**Repo:** `g0b1-firmware` (new `heater-common/`, `heater-coproc/`; G0B1 client in `App/services/`)

## 1. Purpose and intent

A small coprocessor sits between the STM32G0B1 APU controller and one diesel
heater. It owns the heater-specific serial protocol and the long-running
start / run / cooldown state machine, and exposes a simple, heater-agnostic
command/status register map to the G0B1 over I²C (slave 0x64).

The EF-L011 board (STM32L011F4U3TR) is a **temporary hardware-validation
platform**. Production will use an **STM32C011** on the G0B1 board. The
protocol, state-machine, and register-map code must therefore be portable C
with no MCU headers, so the C011 port replaces only a thin port layer.

Success = on R0/R1 G0B1 hardware, the G0B1 can start, stop, and monitor both a
VEVOR 3-wire and an AUTOTERM 4-wire heater through the L011 — with the heater
type and wiring **detected automatically** from whatever is plugged into the
heater header — using non-blocking firmware that ports to the C011.

### Decisions made during brainstorming

| # | Decision |
|---|---|
| D1 | Scope = L011 firmware **and** the G0B1 client that replaces the WK2132/VEVOR path. Cortex UI/agent changes are a separate follow-up. |
| D2 | G0B1 loss: **controlled stop after a timeout** (default 10 min, configurable) — normal protocol stop + cooldown, never a power cut. |
| D3 | AUTOTERM control = verified **setpoint °C + automatic power**; the G0B1 supplies the cabin temperature (forwarded as message 0x11). LEVEL 1–10 applies to level-type heaters (VEVOR). AUTOTERM fixed-power mode is out of scope. |
| D4 | Code lives in `g0b1-firmware` (shared portable code + existing Unity/CMake host suite). |
| D5 | Hardware layer below HAL (full HAL does not fit 16 KB) + HAL-free portable core. *Planning refinement:* the L011 port uses CMSIS register access directly (smaller than LL, measured 10.4 KB flash / 1.5 KB RAM) and builds with a plain Makefile like `boot/`, not a CubeIDE project. |
| D6 | Multi-heater by design: every heater is a driver behind one vtable; adding one touches no FSM / register-map / G0B1 / cortex code. |
| D7 | **Auto-detection is the primary mode**: the header accepts a 3-wire or 4-wire heater and the firmware determines wiring class and protocol itself. Forced type is a bench override. |

## 2. Hardware facts — provisional, schematic is authoritative

**Rule:** the finished EF-L011 schematic is the single source of truth for
(a) which physical UART serves the 3-wire vs 4-wire side, and (b) the exact
function, active level and sequencing of HTR_TX_MODE, HTR_TX_nEN,
HTR_TX_PWR / HTR_5V_EN, GPIOX6 and GPIOX7. None of these are hard-coded in
`core/` or `heater-common/`. They live in **one board file,
`heater-coproc/port/l011/htr_board.h`**, which the implementer fills from the
finished schematic (and the C011 production schematic gets its own copy):

```c
/* wiring class -> UART instance, pins, AF, TX/RX inversion */
#define HTR_BOARD_3W_UART      ...   /* e.g. LPUART1 */
#define HTR_BOARD_4W_UART      ...   /* e.g. USART2  */
#define HTR_BOARD_3W_TX_INVERT ...
/* control lines: pin + active level + role; sequencing order is data, not code */
#define HTR_BOARD_PIN_TX_MODE  ...  /* level that selects the 3-wire path */
#define HTR_BOARD_PIN_TX_nEN   ...  /* level that enables the TX path      */
#define HTR_BOARD_PIN_TX_PWR   ...  /* level that powers the TX stage      */
#define HTR_BOARD_PIN_GPIOX6   ...  /* role per schematic; default hi-Z    */
#define HTR_BOARD_PIN_GPIOX7   ...
```

The table below is what the **R0.00.00 PDF** appears to show. It seeds
`htr_board.h` and the bench checks, and every row is confirmed (or
corrected) against the finished schematic before Plan task "board config" is
closed. Where a row is an inference (pull direction, polarity) it is marked.

| Signal | Pin | Function (as read from R0.00.00) | Reset / safe state |
|---|---|---|---|
| HTR3v3_3W_RX | PA0 LPUART1_RX | 3-wire RX: HTR_DATA_A → divider → LM393 U5A vs 3.0 V ref | input |
| HTR3v3_3W_TX | PA1 LPUART1_TX | 3-wire TX: R25 → Q5 NMOS open-drain → R21 → TMUX1072 NC1 → HTR_DATA_A | analog/hi-Z; Q5 gate pulled down (R29) = line released |
| HTR3v3_4W_TX | PB6 USART2_TX | 4-wire TX: SN74LVC1T45 (3.3→5 V) → FAN4174 (1.67 V / 4.17 V) → R18 → TMUX1072 NO1 → HTR_DATA_A | analog/hi-Z |
| HTR3v3_4W_RX | PB7 USART2_RX | 4-wire RX: HTR_DATA_B → divider → LM393 U5B | input |
| HTR_TX_MODE | PA6 | TMUX1072 SEL: low = NC (3-wire open-drain), high = NO (4-wire driver) | R20 default |
| HTR_TX_nEN | PA7 | TMUX1072 OE, active low | R23 default = disabled |
| HTR_5V_EN | PB1 | XC6705 LDO CE → +5V_HTR (12 V in) feeds the 4-wire driver | R30 pull-down = off |
| I2C1 SCL/SDA | PA9 / PA10 | slave 0x64 on the G0B1's I2C2 bus (via HDR2), 2.2 k pull-ups | — |
| GPIOX6/X7 | PA4 / PA5 | appear to go to test points TP1/TP2 only; final role per schematic | hi-Z (analog) until the schematic defines them; optional debug-toggle build flag only if the schematic confirms they're unloaded |
| SWDIO/SWCLK | PA13 / PA14 | SWD (HDR1) | — |
| BOOT0 | PB9 | R4 pull-down; HPROC_BOOT0 from HDR2 | — |
| NRST | — | HPROC_nRST from HDR2 (G0B1-driveable) | — |

Notes:
- The brief calls PB1 "HTR_TX_PWR"; the R0.00.00 net is **HTR_5V_EN** (LDO enable). The finished schematic decides the name and role; code refers to it only via `HTR_BOARD_PIN_TX_PWR`.
- UART assignment (LPUART1 = 3-wire, USART2 = 4-wire) is read from the R0.00.00 net labels and is **provisional** until the finished schematic confirms it; swapping it is a `htr_board.h` change only (drivers select by wiring class, never by peripheral).
- The schematic sheet title says "STM32C011F6U3TR" but U2 is the L011 — a title-block leftover.
- **HTR_DATA_A** is the 3-wire data wire **and** the 4-wire controller→heater wire (white). **HTR_DATA_B** is the 4-wire heater→controller wire (green). Both TX paths reach HTR_DATA_A only through the mux.
- 3-wire TX is electrically inverting (MCU high → Q5 on → wire low), so LPUART1 uses `TXINV`. RX is not inverted (matches the proven FT232 configuration).
- The RGB driver **IS31FL3293 at 0x6C** shares the bus; the G0B1 drives it directly. The coprocessor never touches it. 0x64 does not collide.
- Pull directions of R20/R23, MODE/nEN/5V_EN active levels and comparator polarity above are **inferences** from R0.00.00 — confirmed against the finished schematic, then scoped at M2/M5. All of them live only in `htr_board.h`.

MCU budget: STM32L011F4 = **16 KB flash / 2 KB RAM**, HSI16 (no crystal). Target ≤ 12 KB flash, ≤ 1.5 KB RAM including a 512 B stack.

## 3. Architecture

```
g0b1-firmware/
├── heater-common/                 portable, no MCU headers; built by both MCUs
│   ├── htr_i2c_map.h              frozen I²C contract (coprocessor + G0B1 include it)
│   ├── htr_types.h                htr_request_t, htr_status_t, phase/caps/valid enums
│   ├── htr_uart.h                 byte-stream seam: write / read / flush / set_baud / tx_idle
│   ├── crc16_modbus.c/.h
│   ├── vevor_proto.c/.h           moved from App/services/vevor_heater.c; WK2132_t → htr_uart_t,
│   │                              frame bytes/timing unchanged
│   └── autoterm_proto.c/.h        new
├── heater-coproc/
│   ├── core/                      portable, host-tested
│   │   ├── htr_driver.h           driver vtable + registry
│   │   ├── htr_fsm.c/.h           generic state machine + auto-detect
│   │   ├── htr_iface.c/.h         routing / power sequencing via a GPIO seam
│   │   ├── htr_regs.c/.h          register file, snapshot, command dispatch, host timeout
│   │   └── htr_nv.h               non-volatile settings seam
│   ├── port/l011/                 the ONLY MCU/board-specific code (LL)
│   │   ├── htr_board.h            pins, UART↔wiring map, active levels (from schematic)
│   │   ├── port_uart.c            LPUART1 + USART2 IRQ, 64 B RX / 32 B TX rings, echo strip
│   │   ├── port_i2c_slave.c       I2C1 slave IRQ state machine
│   │   ├── port_gpio.c, port_tick.c (SysTick 1 ms), port_nv.c (data EEPROM)
│   │   ├── main.c                 init + superloop
│   │   ├── Makefile, l011.ld      arm-none-eabi build + 16 KB/2 KB size gate
│   │   └── vendor/cmsis_device/   ST cmsis_device_l0 v1.9.4 (pinned)
├── App/services/heater_coproc.c/.h  G0B1 I²C client (replaces wk2132.c + vevor_heater.c)
└── Tests/                         new host tests + fixtures
```

Superloop (no blocking anywhere): `regs_poll(now)` → `fsm_process(now)` →
`driver->process(now)` → `regs_publish(now)` → IWDG refresh (≈2 s, armed after
init, frozen in debug). ISRs only move bytes between peripherals and rings.

**C011 port** = new `port/c011/` (own `htr_board.h`, Makefile, linker script). `heater-common/` and
`heater-coproc/core/` move unchanged; the register map is identical except `HW_ID`.

### 3.1 Driver interface (multi-heater)

```c
typedef struct {
    uint8_t     type_id;      /* HTR_TYPE_VEVOR=1, HTR_TYPE_AUTOTERM=2, next=3 ... */
    uint8_t     wiring;       /* HTR_WIRING_3W | HTR_WIRING_4W: selects UART + mux mode */
    uint16_t    caps;         /* CAP_LEVEL, CAP_SETPOINT, CAP_AMBIENT_IN, CAP_FAN_RPM,
                                 CAP_PUMP, CAP_SUPPLY_V, CAP_EXCH_TEMP, CAP_FAULT_CODE */
    void       *ctx;          /* driver-private state */
    void (*init)(void *ctx, htr_uart_t *u, uint32_t now);
    void (*process)(void *ctx, uint32_t now);
    void (*start)(void *ctx, const htr_request_t *rq);
    void (*stop)(void *ctx);                           /* requests stop; never cuts power */
    void (*apply)(void *ctx, const htr_request_t *rq); /* live level/setpoint/ambient */
    htr_probe_t (*probe)(void *ctx, uint32_t now);     /* auto-detect step: BUSY/FOUND/ABSENT */
    void (*status)(const void *ctx, uint32_t now, htr_status_t *out);
} htr_driver_t;

static const htr_driver_t *const g_drivers[] = { &vevor_driver, &autoterm_driver };
```

- `htr_request_t` = { run, level 1–10, setpoint_c, ambient_c_x10 }; each driver uses what its `caps` declare.
- `htr_status_t` = { vendor_state, vendor_sub, phase_hint, fault_code, supply_mv, fan_rpm, pump_hz_x10, exch_raw, exch_c_x10, state_seconds, last_rx_tick, counters, `valid` bitmask }. Fields not reliably decoded stay **invalid** — never a guessed value.
- **Probe rule:** `probe()` may only transmit requests that cannot start a heater or change its settings (version, status, or that driver's standby/stop poll). Enforced by review + a per-driver host test asserting the bytes probe sends.
- Adding a heater = one `xyz_proto.c` + host tests + one `g_drivers[]` line.

### 3.2 Interface sequencing (`htr_iface`)

- **Reset:** pins analog/hi-Z, LDO off (R30), mux disabled (R23) — safe by hardware before firmware runs. Firmware's first act sets every control line to its safe level explicitly.
- **enable(wiring):** (1) `nEN` inactive (mux off); (2) `MODE` = wiring; (3) `5V_EN` on if 4-wire (3-wire need for +5V_HTR is bench-confirm; default off) and wait 5 ms; (4) configure UART AF, inversion, baud — TX at idle; (5) `nEN` active.
- **disable:** exact reverse. A wiring change always passes through disable.
- **During line sensing** (auto-detect step 1) the mux stays disabled and UART pins are plain inputs.

### 3.3 UARTs (assignment per `htr_board.h`; R0.00.00 reading shown)

| Port | Peripheral | Wiring | Format | Notes |
|---|---|---|---|---|
| HTR_PORT_3W | LPUART1 (PA0/PA1) | 3-wire | 8N1, TXINV=1, RXINV=0 | Kernel clock = PCLK1 8 MHz (APB1 /2) so BRR covers 2400–9600 (at 16 MHz, 2400 overflows the 20-bit BRR). Half-duplex echo: port records sent bytes; RX ISR drops matching echo, mismatch → `ECHO_ERRS++`, `ECHO_FAULT`. |
| HTR_PORT_4W | USART2 (PB6/PB7) | 4-wire | 8N1, no inversion | `set_baud()` applied only when `TC` set (TX idle). |

## 4. Generic state machine (`htr_fsm`)

```
BOOT ──(3 s TX-quiet)──► DETECTING ──found──► OFF ◄──────────────────────────┐
                              ▲                │ RUN_REQUEST=1               │ fresh standby (SAFE_OFF)
                     hot-swap │                ▼                             │
                    (AUTO, 30 s              STARTING ──phase RUNNING──► RUNNING
                     lost while OFF)           │                             │ RUN_REQUEST=0
                                               │ start timeout 10 min        │ or host timeout
                                               ▼                             ▼
                                             FAULT ◄───vendor fault─── STOP_REQUESTED
                                               │                             │ heater leaves running
                                   CLEAR_FAULT │                             ▼
                                   + standby   └────────► OFF ◄──────── COOLDOWN
```

Phases (register value): 0 OFF, 1 DETECTING, 2 STARTING, 3 RUNNING, 4 STOP_REQUESTED, 5 COOLDOWN, 6 FAULT.

Rules:
1. **No autonomous restart.** Entering FAULT clears RUN_REQUEST; the driver keeps polling/sending stop (re-sent, rate-limited, while the heater still reports starting/running). Restart needs `CLEAR_FAULT` (accepted only when the heater reports standby) then a new RUN_REQUEST. A HEATER_TYPE change is refused (BUSY) in FAULT so it cannot bypass that check.
1a. **Never leave a heater burning unrequested.** In OFF with no run request, a fresh starting/running report → `driver->stop()` and STOP_REQUESTED (covers a coprocessor reset mid-run and a start processed after a no-op stop). STOP_REQUESTED → OFF needs a standby report at least `HTR_STOP_SETTLE_MS` (1.5 s) newer than the stop; the AUTOTERM driver likewise acks a stop only on a status ≥ 1 s after its first stop frame.
1b. **Failed start.** In STARTING, a cooldown report, or a standby report after the heater had begun starting, → FAULT immediately (not after the 10-min timeout). A non-zero VEVOR error code is a FAULT hint (code values bench-confirm).
2. **Comms loss is a flag**, not a transition: `COMMS_FAULT` = run requested ∧ ¬FRESH. The driver keeps sending its last request.
3. **Never stop by removing power.** Every stop path (G0B1 command, host timeout, fault) goes through `driver->stop()` and cooldown. `5V_EN`/mux are only dropped when phase is OFF or DETECTING.
4. **Boot TX-quiet:** no transmission for 3 s after reset, so the G0B1 can re-assert RUN_REQUEST before any probe/standby poll reaches a possibly running heater.
5. **Unexpected stop:** in RUNNING with run still requested, a fresh heater report of cooldown or standby (the heater stopped on its own) → FAULT. *Bench item: confirm AUTOTERM does not idle to standby at setpoint in auto-power mode (open item 5).*
6. **Timeouts:** STARTING > 10 min → FAULT. COOLDOWN > 15 min → FAULT (still polling). STOP_REQUESTED: stop re-sent every 1 s until the heater leaves its running states.
7. **OFF keeps polling** (VEVOR 1 Hz stop poll; AUTOTERM 0x0F) so PRESENT / FRESH / SAFE_OFF stay current.
8. **Freshness:** FRESH = valid frame within the last 3 s.

### 4.1 Auto-detection (phase DETECTING)

1. **Passive line sense (~1 s, no TX):** mux disabled, sample both comparator outputs.
   - DATA_B idle-high → wiring class **4-wire** (heater TX line idling).
   - DATA_A idle-high, DATA_B low → **3-wire** (heater's own pull-up on its data wire).
   - neither → no heater/unpowered: retry every 10 s, PRESENT clear.
2. **Electrical safety rule:** the 4-wire push-pull driver is enabled **only** after step 1 saw the 4-wire signature. The 3-wire open-drain path is safe on either class.
3. **Probe** each registered driver of that wiring class, in table order; first FOUND wins → `ACTIVE_TYPE`, `DETECT_DONE`.
   - 4-wire: AUTOTERM wake + version request (§5.2); FOUND on a CRC-valid `AA 04 … 06` reply.
   - 3-wire: VEVOR standby (stop) poll at 4800 inverted; FOUND on a checksum-valid 56-byte reply.
4. **Ambiguous line** (e.g. a heater whose TX doesn't idle-high while asleep): run only the 3-wire-safe probes; on failure report `DETECT_RESULT = UNKNOWN` — never guess; a forced type can be set.

Triggers: boot with `HEATER_TYPE = AUTO` (default); `AUTO_DETECT` command; hot-swap (AUTO, link lost 30 s **while OFF** — never while a heater may be running).

Known consequence: VEVOR answers only control frames, so if the coprocessor resets while a VEVOR is running, the probe's stop poll sends it into normal cooldown (safe); the G0B1's re-asserted RUN_REQUEST restarts it.

## 5. Protocol drivers

### 5.1 VEVOR XMZ-F-D5 (3-wire) — VERIFIED

Robb's verified protocol (`docs/vevor-heater.md`, 1221-frame capture regression) moved unchanged to `heater-common/vevor_proto.c`: 4800 8N1 one-wire, 16-byte controller frames (start ×4 burst, run, stop), 56-byte heater frames. Only the transport seam changes (`WK2132_t` → `htr_uart_t`).

- caps: LEVEL, FAN_RPM, PUMP, SUPPLY_V, EXCH_TEMP (raw only), FAULT_CODE.
- Vendor state → phase hint: 0 off → OFF, 1 preheat / 2 ignition → STARTING, 3 running → RUNNING, 4 cooldown → COOLDOWN.

### 5.2 AUTOTERM (4-wire)

**Framing (verified, all reference frames + 712 captured replies CRC-valid):**
`AA dir LEN 00 CMD payload[LEN] CRC_HI CRC_LO` — dir 0x03 controller→heater, 0x04 heater→controller; CRC-16/MODBUS over `AA … payload`, transmitted **high byte first** (the bench notes' "CRC_LO CRC_HI" ordering is incorrect; e.g. `AA 03 00 00 0F` → CRC 0x587C → `58 7C`).

**Verified request set:**

| Purpose | Frame |
|---|---|
| Wake (2400) | `AA 03 00 00 1C 95 3D` |
| Version / discovery | `AA 03 00 00 06 5E BC` |
| Status | `AA 03 00 00 0F 58 7C` |
| Settings read | `AA 03 00 00 02 9D BD` |
| Controller temp (23 °C) | `AA 03 01 00 11 17 B3 11` |
| Start (29 °C reference) | `AA 03 07 00 01 FF FF 02 1D 01 FF 11 19 34` |
| Stop | `AA 03 00 00 03 5D 7C` |
| Settings write | `AA 03 06 00 02 FF FF 02 TT 01 FF <crc>` |

TT = setpoint, whole °C. Start is reproduced byte-exact with **only** TT parameterised (payload bytes 1–3 and 5–7 copied verbatim from the capture, including the trailing 0x11 whose meaning is unknown).

**Wake / link sequence:** 2400 baud: wake frame ×35 @ 200 ms → 4800: version ×5 @ 200 ms → 9600: version until a valid reply (3 s timeout → restart wake; 3 failed wakes → not present). Tolerate the malformed `03 AA …` transition capture (parser resyncs on `AA`).

**Online poll cycle (9600):** status `0F` every 1 s; settings `02` read every 5 s; controller temp `11` every 10 s (only when the G0B1 has supplied a valid ambient). `0x04` / `0x0B` are **not sent** unless bench evidence shows the heater requires them.

**Control:** start → start frame; setpoint change while on → settings write; stop → `03` re-sent every 1 s until the status leaves running states, then keep polling through cooldown.

**Status reply** `AA 04 13 00 0F` + 19 payload bytes (from `autoterm.csv`, 712 frames, one full cycle):

| Byte | Field | Status |
|---|---|---|
| 0 / 1 | state major / minor | **verified** — observed 0.1 → 1.0 → 2.0 → 2.1 → 2.2 → 2.4 → 4.0 → 0.1 |
| 3 | heater internal temp, °C | provisional (heater-internal sensor, not cabin) |
| 4 | external sensor, °C; 0x7F = absent | 0x7F = absent verified |
| 6 | supply voltage ×0.1 V | **verified** (12.6 V idle, 11.1 V under glow load) |
| 7–8 | exchanger temp raw, u16 big-endian | raw only (prior tool used raw−271 °C; unconfirmed) |
| 9 | unknown (0 standby, 1 run, 5 shutdown) | raw only |
| 11 / 12 | fan requested / actual, rev/s (×60 rpm) | provisional |
| 14 / 16 | pump requested / actual, ×0.01 Hz | provisional |
| 17, 18 | unknown | raw only |

Provisional fields are carried internally but their `VALID` bits stay clear until a bench check confirms them; supply voltage and state are valid.

**Phase hint mapping (evidence-based):** major 0 → OFF; major 1, or 2.0–2.3 → STARTING; **2.4+ or any 3.x → RUNNING** (steady full-power code was not captured — bench-confirm, M7); **major 4 → COOLDOWN** (verified; captured cooldown 3.1 min); any other major → FAULT with raw state exposed. Captured timing: start→2.4 ≈ 3.2 min; 4.0 → 0.1 ≈ 3.1 min.

caps: SETPOINT, AMBIENT_IN, SUPPLY_V, FAN_RPM, PUMP, EXCH_TEMP (raw), FAULT_CODE (once decoded).

## 6. I²C register map (`htr_i2c_map.h`, MAP_VERSION 1)

Slave 0x64, 7-bit. Register-pointer protocol: master writes a 1-byte index, then reads/writes; index auto-increments (G0B1 uses `HAL_I2C_Mem_Read/Write`). u16/s16 little-endian. On an address-match for a read, the whole status block is copied to a shadow buffer so one transaction is internally consistent (no torn values). Writes to RO registers are ignored; out-of-range values are rejected (register unchanged) and reported in `CMD_RESULT`. Any transaction addressed to 0x64 resets the host-idle timer.

**Identity (RO)**

| Reg | Name | Value |
|---|---|---|
| 0x00 | DEVICE_ID | 0xEF |
| 0x01 | MAP_VERSION | 1 |
| 0x02 | FW_MAJOR | |
| 0x03 | FW_MINOR | |
| 0x04 | FW_BUILD (u16) | |
| 0x06 | SUPPORTED_TYPES (u16) | bit n = driver type n compiled in |
| 0x08 | HW_ID | 1 = EF-L011, 2 = C011 production |

**Control (RW unless noted)**

| Reg | Name | Range / meaning |
|---|---|---|
| 0x10 | HEATER_TYPE | 0xFF AUTO (default), 0 none, 1 VEVOR, 2 AUTOTERM; persisted |
| 0x11 | RUN_REQUEST | 0/1, level-triggered, idempotent |
| 0x12 | LEVEL | 1–10 (CAP_LEVEL) |
| 0x13 | SETPOINT_C | s8, 5–30 °C (CAP_SETPOINT) |
| 0x14 | AMBIENT_C_X10 (s16) | −400…+850; 0x7FFF = unknown |
| 0x16 | HOST_TIMEOUT_MIN | 1–60, default 10; 0 = disabled (bench only); persisted |
| 0x18 | COMMAND (WO) | 1 START, 2 STOP, 3 CLEAR_FAULT, 4 REINIT_IFACE, 5 AUTO_DETECT |
| 0x19 | CMD_RESULT (RO) | 0 OK, 1 BUSY, 2 BAD_ARG, 3 NOT_SUPPORTED, 4 NO_TYPE |

START/STOP set/clear RUN_REQUEST, so command and request never disagree.

**Status (RO, snapshotted)**

| Reg | Name | Notes |
|---|---|---|
| 0x20 | PHASE | §4 |
| 0x21 | ACTIVE_TYPE | detected or forced |
| 0x22 | FLAGS (u16) | below |
| 0x24 | VALID (u16) | bit per field 0x28–0x34 |
| 0x26 | VENDOR_STATE | raw major |
| 0x27 | VENDOR_SUB | raw minor |
| 0x28 | FAULT_CODE | raw vendor |
| 0x29 | ACTIVE_LEVEL | |
| 0x2A | SUPPLY_MV (u16) | |
| 0x2C | FAN_RPM (u16) | |
| 0x2E | PUMP_HZ_X10 (u16) | |
| 0x30 | EXCH_RAW (u16) | |
| 0x32 | EXCH_C_X10 (s16) | |
| 0x34 | STATE_SECONDS (u16) | |
| 0x36 | AGE_MS (u16) | saturating |
| 0x38 | VALID_FRAMES (u16) | |
| 0x3A | CRC_FAILS (u16) | saturating |
| 0x3C | TRANSPORT_ERRS (u16) | saturating |
| 0x3E | ECHO_ERRS (u16) | saturating |
| 0x40 | CAPS (u16) | active driver |
| 0x42 | HOST_IDLE_S (u16) | |
| 0x44 | LINE_SENSE | bit0 DATA_A idle-high, bit1 DATA_B idle-high, bits4–5 class 0 none / 1 3W / 2 4W / 3 ambiguous |
| 0x45 | DETECT_RESULT | 0 idle, 1 running, 2 found, 3 no heater, 4 unknown |

FLAGS: bit0 PRESENT, 1 FRESH, 2 SAFE_OFF, 3 COMMS_FAULT, 4 IFACE_POWERED, 5 HOST_TIMEOUT_STOP, 6 ECHO_FAULT, 7 DETECT_DONE, 8 FAULT_LATCHED.

**Host timeout (D2):** if RUN_REQUEST = 1 and HOST_IDLE_S ≥ HOST_TIMEOUT_MIN×60, clear RUN_REQUEST, run the normal stop/cooldown, set HOST_TIMEOUT_STOP (cleared by the next G0B1 write of RUN_REQUEST).

**Persistence:** HEATER_TYPE and HOST_TIMEOUT_MIN via `htr_nv` → L011 data EEPROM; C011 port emulates in one flash page. RUN_REQUEST is never persisted.

## 7. G0B1 client and Modbus

**`App/services/heater_coproc.c`** replaces `wk2132.c` + `vevor_heater.c`; `heater.c` keeps its public API so `mbp_heater.c` / `app_main.c` change minimally.

- Bus: I2C2 PA11/PB14 (former DFR0627 pins, now HDR2) via the RTC-style `i2c_backend_t` seam → host-testable against a fake 0x64.
- Boot: read DEVICE_ID / MAP_VERSION. No ACK or wrong ID → `heater_present = 0` (absent, not a fault). Unknown MAP_VERSION → flag, identity reads only, no control.
- Every 250 ms: write control block 0x11–0x15 (RUN_REQUEST, LEVEL, SETPOINT, AMBIENT) — rewriting every cycle keeps the host timer alive and self-heals a coprocessor reset — then one `mem_read` of 0x10–0x45 (control readback incl. the persisted HEATER_TYPE, plus status). HEATER_TYPE is written only on an explicit reg-74 write.
- AMBIENT = G0B1 cabin temp (°F → °C×10). **Caveat:** today's cabin value is the PA0 enclosure stand-in, so AUTOTERM room regulation keys off enclosure temperature until a cab sensor exists.
- A coprocessor FAULT latches the G0B1's own request off (`heater_request` reads 0); a new ON write re-arms. So neither a coprocessor reset nor CLEAR_FAULT can re-ignite on a stale ON.
- No-ACK before the coprocessor was ever identified is *absent*: no XPORT_FAULT, no transport-error count.
- Recovery: if the coprocessor is silent 5 s, drop back to identification and call an optional `reset_coproc` hook (pulses HPROC_nRST once a free G0B1 GPIO is assigned — open item 6; NULL until then).

**Modbus (firmware reg N = wire N−1).** Regs 53–67 keep their frozen meaning so cortex v1.2.40+ works unchanged:
- reg 55 `state` = generic phase collapsed to legacy codes: OFF/DETECTING/FAULT → 0, STARTING → 1, RUNNING → 3, STOP_REQUESTED/COOLDOWN → 4 (FAULT additionally sets reg 57 `error` = vendor fault code).
- reg 64 `flags` bits 0–4 unchanged; bit 4 XPORT_FAULT now = G0B1↔coprocessor I²C failure.

New (replaces the 68–70 reservation; `MB_REG_MAX` 70 → 75, `MB_REG_LIMIT` 71 → 76):

| fw reg | Name | RW | Meaning |
|---|---|---|---|
| 68 | heater_type | RO | ACTIVE_TYPE |
| 69 | heater_setpoint_c | RW | SETPOINT_C |
| 70 | heater_caps | RO | CAPS |
| 71 | heater_phase | RO | full phase 0–6 |
| 72 | heater_vendor_state | RO | major<<8 \| sub |
| 73 | heater_detect | RO | DETECT_RESULT<<8 \| LINE_SENSE |
| 74 | heater_type_select | RW | HEATER_TYPE (0xFF AUTO) |
| 75 | heater_command | RW | write 1 start, 2 stop, 3 clear-fault, 4 reinit, 5 auto-detect (forwarded once to COMMAND); read = last CMD_RESULT |

## 8. Testing

**Host (Unity + CMake, ASan/UBSan, existing `Tests/` suite):**
- `crc16_modbus`: the 9 reference frames + all 712 captured replies.
- `autoterm_proto`: byte-exact builders (wake, version, status, settings read/write, ambient 23 °C, start 29 °C, stop); parser resync on garbage, bad-CRC rejection, `03 AA …` transition; replay of `Tests/fixtures/autoterm_cycle.csv` reproduces the phase timeline OFF → STARTING → RUNNING → COOLDOWN → OFF; probe sends only wake/version.
- `vevor_proto`: existing 1221-frame regression unchanged; probe sends only the stop poll.
- `htr_fsm` (fake drivers + clock): every transition; start/cooldown timeouts; no autonomous restart after FAULT; host-timeout stop goes via cooldown; comms loss = flag only; boot TX-quiet; auto-detect per wiring class, 4-wire driver never enabled without the 4W signature, ambiguous → UNKNOWN, hot-swap only while OFF.
- `htr_iface`: enable/disable ordering against a recording fake GPIO.
- `htr_regs`: snapshot consistency, RO ignore, range reject, CMD_RESULT, persistence via fake nv.
- G0B1 `heater_coproc`: fake 0x64 — identity/version gating, absent ≠ fault, Modbus 53–74 mapping, legacy state collapse.

**Bench milestones (user-run, each gates the next):**

| | Proves |
|---|---|
| M1 | L011 boots on HSI16 via SWD; GPIOX6/X7 toggle; IWDG stable; flash/RAM within budget |
| M2 | Scope 5V_EN / MODE / nEN at reset (safe) and through enable/disable; confirm R20/R23 pull directions |
| M3 | G0B1 reads DEVICE_ID at 0x64 and round-trips registers; IS31FL3293 still ACKs at 0x6C |
| M4 | LPUART1 and USART2 independently (loopback/FT232); 3-wire TX inversion + echo strip; USART2 2400/4800/9600 switching |
| M5 | LINE_SENSE with nothing / VEVOR / AUTOTERM connected matches §4.1 assumptions (incl. AUTOTERM TX idle level while asleep; open-input reads low) |
| M6 | VEVOR: auto-detect → SAFE_OFF → **fueled** start, level changes, full cooldown, all from the G0B1 |
| M7 | AUTOTERM: auto-detect → start at setpoint → **record steady-run state code** → stop → cooldown to 0.1; confirm whether 0x04/0x0B are needed |
| M8 | Cortex HeaterCard → Modbus → G0B1 → coprocessor → heater; then cut I²C while running and observe the host-timeout stop + cooldown |

## 9. Out of scope

- Cortex agent/UI changes for AUTOTERM fields (follow-up; existing HeaterCard keeps working via regs 53–67).
- The C011 port (`port/c011/`) — structure is ready, implementation later.
- Programming the coprocessor from the G0B1 (I²C/UART bootloader).
- RGB control (G0B1 drives IS31FL3293 at 0x6C directly).
- AUTOTERM fixed-power mode; decoding 0x04 / 0x0B; AUTOTERM fault-code table.

## 10. Open items (bench-confirm)

1. **From the finished schematic:** 3-wire vs 4-wire UART assignment; function, active level and sequencing of HTR_TX_MODE, HTR_TX_nEN, HTR_TX_PWR/HTR_5V_EN, GPIOX6, GPIOX7 → `htr_board.h`. Then scope-confirm at M2.
2. Whether the 3-wire path needs +5V_HTR.
3. LM393 RX polarity on both channels (assumed non-inverting).
4. AUTOTERM TX idle level while asleep (drives auto-detect); open-input line levels.
5. AUTOTERM steady full-power state code; whether it idles to standby at setpoint in auto-power mode (would trip the unexpected-stop FAULT rule); fault codes; scaling of temp/fan/pump fields.
6. Free G0B1 GPIO for HPROC_nRST on R0/R1; HDR2 I2C2 = PA11/PB14 on R0/R1.
7. Flash/RAM fit on the L011 (estimate ~10–12 KB / ~1.2 KB).
