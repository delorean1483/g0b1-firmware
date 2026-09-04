# VEVOR XMZ-F-D5 Diesel Heater — Firmware Bring-Up, Register Map, and Safety

**Purpose:** control a VEVOR XMZ-F-D5 diesel air heater by relaying start/stop/level commands to the heater's own ECU and reporting its telemetry over Modbus. The heater ECU retains full control of combustion, glow plug, fuel pump, fan, ignition, faults, and multi-minute startup/cooldown sequences. The STM32 acts as a relay and reporter—never a combustion controller.

---

## 1. Overview

The VEVOR heater protocol was **reverse-engineered from real device captures** and verified against captured frames: **1221 heater frames analyzed, 0 checksum failures**. The firmware encodes start/stop and level requests into 16-byte UART frames and decodes heater telemetry (state, temperatures, RPM, supply voltage, error codes) from device responses, all over a dedicated UART at **4800 baud 8N1**.

The heater ECU is the **sole authority** over:
- Combustion safety (ignition/glow sequence, fuel injection timing, ignition lock-out)
- Forced fault shutdown (overtemp, low supply, pump/fan fault)
- Multi-minute startup ramp (preheat, glow, ignition hold, warm-up, stabilization)
- ~5-minute cooldown sequence (mandatory shutdown ritual)

The STM32 firmware **never commands** combustion or forces a power cut. It sends mode (on/off/level) requests and **always polls actively**; loss of heater response is flagged as a fault, and the response itself is the authority on safe state and readiness to power down.

---

## 2. Hardware / Wiring

### I2C-to-UART Bridge (WK2132)

Both STM32 UARTs are committed (USART1 = RS-485 APU, LPUART1 = Ezurio radio), so the heater UART is provided by a **DFRobot DFR0627** / **WK2132-ISSG** I2C-to-UART bridge on the following pins:

| Signal | Pin | Function |
|--------|-----|----------|
| I2C2_SCL | **PA11** | SCL, open-drain |
| I2C2_SDA | **PB14** | SDA, open-drain |
| (WK2132) | I2C addr **0x70** | channel 0; DIP A1=1, A0=1 (verify on board) |
| (WK2132 UART1) | — | 4800 8N1, baud generator on-device |

### Power and Bus Routing

- **Power:** DFR0627 draws 3.3 V from the **switched 3P3_VCC rail** (confirmed stable before firmware init). The firmware initializes the bridge **only after that rail is stable** at boot.
- **Data line:** The heater's one-wire data line **idles high at ~3.86 V** and is open-drain/wired-OR with the factory VEVOR display. A **non-inverting open-drain stage** (e.g. SN74LVC1G07, or cascaded NPN emitter-follower) must sit **between the DFR0627 TX and the heater data line**. This is essential: the DFR0627 cannot drive push-pull into a 3.86 V idle bus.
- **RX level-limiting:** The DFR0627 RX must be level-limited from the ~3.86 V heater bus to safe 3.3 V (voltage divider or open-drain buffer).
- **Factory display:** **must be disconnected** while the STM32 is transmitting. Two active masters on the same open-drain bus will corrupt frames.

---

## 3. Required CubeMX / .ioc Configuration

The firmware ships the **driver** (`drv_wk2132_i2c2.c`) and wiring harness, but the **I2C2 peripheral itself must be configured in CubeMX**—it cannot be scripted:

### Steps

1. Open the project in **STM32CubeIDE**, then open **CubeMX** (left pane, double-click `.ioc`).
2. **Enable I2C2** with the following pins and settings:
   - **PA11** → I2C2_SCL, **open-drain, pull-up disabled** (the bus is 100 kΩ pull-up by the WK2132)
   - **PB14** → I2C2_SDA, **open-drain, pull-up disabled**
   - **I2C clock:** 100 kHz (standard mode)
   - **Confirm neither PA11 nor PB14 are assigned to any other peripheral**
3. **Generate code** (CubeMX → Project → Generate Code). This creates `MX_I2C2_Init()` in `main.c` and `hi2c2` handle.
4. **Close and reopen the project** in CubeIDE (File → Close Project, then File → Open Project). CubeIDE must reload the `.project` file to pick up new linked-resource entries:
   - `firmware/app/wk2132.c` (register sequences)
   - `firmware/app/vevor_heater.c` (protocol/frames)
   - `firmware/app/heater.c` (telemetry FSM)
   - `firmware/app/mbp_heater.c` (Modbus binding)

   Without this Close → Open, the build will fail to find the new sources.

---

## 4. Modbus Register Map

The heater is bound to Modbus **registers 53–67** (firmware `MB_REG_*` enum). Registers 68–70 are reserved for future thermostat "HEAT mode" (setpoint→level mapping). The **MB_REG_MAX was expanded from 52 to 70** in the register module.

| Reg | Symbol | Type | Access | Description |
|-----|--------|------|--------|-------------|
| 53 | `MB_REG_HEATER_REQUEST` | U16 | RW | Heater mode: 0=stop (off), 1=run (on) |
| 54 | `MB_REG_HEATER_LEVEL` | U16 | RW | Heater power level: 1–10 (clamped by firmware) |
| 55 | `MB_REG_HEATER_STATE` | U16 | RO | Heater state code: 0=off, 1=preheat (glow), 2=ignition, 3=running, 4=cooldown |
| 56 | `MB_REG_HEATER_ACTIVE_LEVEL` | U16 | RO | Current active power level (echoed from device during running) |
| 57 | `MB_REG_HEATER_ERROR` | U16 | RO | Error code from heater: 0=none (see heater manual for fault codes) |
| 58 | `MB_REG_HEATER_SUPPLY_MV` | U16 | RO | Supply rail voltage in mV (e.g., 24000 = 24 V) |
| 59 | `MB_REG_HEATER_FAN_RPM` | U16 | RO | Heater fan speed in RPM |
| 60 | `MB_REG_HEATER_PUMP_HZ_X10` | U16 | RO | Fuel pump frequency × 10 (e.g., 450 = 45 Hz) |
| 61 | `MB_REG_HEATER_EXCHANGER_RAW` | U16 | RO | Exchanger temperature raw ADC value (unconverted; future task) |
| 62 | `MB_REG_HEATER_STATE_SECONDS` | U16 | RO | Time in current state (seconds), max ~300 s (cooldown/startup) |
| 63 | `MB_REG_HEATER_AGE_MS` | U16 | RO | Age of the telemetry sample (milliseconds since last valid frame) |
| 64 | `MB_REG_HEATER_FLAGS` | U16 | RO | Bit flags (see table below) |
| 65 | `MB_REG_HEATER_VALID_FRAMES` | U16 | RO | Count of valid heater frames received (diagnostic) |
| 66 | `MB_REG_HEATER_CHECKSUM_FAILURES` | U16 | RO | Count of checksum failures (diagnostic) |
| 67 | `MB_REG_HEATER_TRANSPORT_ERRORS` | U16 | RO | Count of transport/framing errors (diagnostic) |
| 68–70 | *reserved* | — | — | Future thermostat "HEAT mode" (setpoint→level mapping) |

### Flags Register (reg 64) Bit Definitions

| Bit | Symbol | Meaning |
|-----|--------|---------|
| 0 | `HEATER_FLAG_FRESH` | Telemetry is fresh (≤3 seconds old); RX/Modbus read clears it |
| 1 | `HEATER_FLAG_COOLDOWN` | Heater is in cooldown sequence (state=4); expected ~5 min after stop-request |
| 2 | `HEATER_FLAG_SAFE_OFF` | Safe to power-cut: only set when a fresh (≤3 s), checksum-valid response reports **state 0 (off)** |
| 3 | `HEATER_FLAG_COMMS_FAULT` | Communication lost (no valid frame for >10 s); **never clears safe_to_power_down while active** |
| 4 | `HEATER_FLAG_XPORT_FAULT` | Framing/parity error on the bus; recoverable, monitored for diagnostic |

---

## 5. Safety Invariants (CRITICAL)

> ⚠️ **The heater ECU owns combustion control and safe shutdown. The STM32 is a relay and reporter. These rules are non-negotiable.**

### Core Rules

1. **Never stop the heater by removing power.** The heater ECU must complete its own shutdown sequence. A forced power cut will leave fuel and glow plug in mid-sequence and may cause damage or ignition hazard.

2. **Loss of communication is a FAULT.** If the heater does not respond for >10 seconds, set `comms_fault` (reg 64 bit3) and continue polling. This is **not** a permission to force-stop or power-cut; it is a diagnostic alert.

3. **Only a fresh, valid state-0 response sets safe-to-power-down.** The bit `HEATER_FLAG_SAFE_OFF` (reg 64 bit2) is set **only when**:
   - A heater response is valid (checksum OK)
   - **Age ≤ 3 seconds** (reg 63 ≤ 3000 ms)
   - **State code = 0 (off)** (reg 55 == 0)
   - All three conditions must be true simultaneously.
   - Any other response (state 1–4, or age >3 s) clears the bit.

4. **On MCU reset, initialize to stop-request but keep polling.** The firmware sets `heater_request = 0` at boot and begins polling immediately. It **never force-restarts or power-cuts a heater it finds already running** (state 1–4). The UI/application layer decides when to request a start.

5. **Mode change while running is a FAULT, not a stop.** If the user changes the request (on→off, or level change during run) and the heater is mid-sequence, the request is transmitted, but the heater controls its own shutdown (forced power cut is forbidden). The MCU continues polling and reports the transitional state.

6. **Comms fault blocks power-down.** While `comms_fault` is active (bit3), `safe_to_power_down` **is guaranteed to be 0** (cleared). The system must not attempt a power cut until comms is restored and a fresh state-0 response is received.

### UI / Host Application Responsibility

- **Present "off" as cooldown-in-progress (~5 minutes), not instantaneous.** The heater's cooldown is mandatory and takes ~5 minutes. The UI must show a cooling-down state or progress indicator, not report "heater off" until state=0 is observed.

- **Poll actively and display the state and flags prominently.** The user must see if `comms_fault` or error codes are set; these are not silent conditions.

- **Never offer a "force off" or "power-cut" button to the user.** If the system must power down, do so only after `safe_to_power_down` is true.

---

## 6. Staged Bench Bring-Up (Hardware Not Yet In Hand)

The following procedure **must be followed in order**. Each stage de-risks a different aspect (power, I2C, UART, bus levels, heater integration, fuel/combustion).

### Stage 1: Power the Bridge, No Heater Attached

1. Flash the firmware to the STM32.
2. Power the board at 3.3 V on the **3P3_VCC rail** (confirm stable with a voltmeter).
3. With **no heater connected**, verify:
   - DFR0627 LED (if present) is lit or stable.
   - **No smoke, no excessive current draw.**

### Stage 2: I2C Scan and WK2132 Register Readback

1. Connect an I2C analyzer or use OpenOCD/GDB to probe the I2C2 bus (PA11 SCL, PB14 SDA).
2. Scan I2C for devices at address **0x70** (DFR0627).
3. Read the **GENA register** (WK2132 register 0x00) and confirm it returns a known ID (e.g., 0x02).
4. Confirm `MX_I2C2_Init()` was called at boot (check the I2C bus with a logic analyzer: should see START condition and ACK).

**Diagnostic:** If no device at 0x70, check:
- PA11/PB14 are correctly assigned in CubeMX (alternate function I2C2).
- DIP switches A1/A0 on the DFR0627 are both set to 1 (verify physical board).
- Power is reaching the bridge (3.3 V on pin 1 or confirmed by LED).

### Stage 3: UART Loopback Test

1. Loopback the WK2132 UART1: **connect TX to RX** on the bridge (e.g., via jumper on the bread board or a small patch).
2. Run the `wk2132` driver test (host test suite) or a simple firmware routine: send a known 16-byte frame and verify it is read back.
3. Confirm:
   - FIFO read/write works (16-byte boundary respected).
   - Baud rate is correct (no garbled data).
   - No overflow or framing errors.

**Diagnostic:** If loopback fails, check:
- WK2132 baud-rate configuration (4800 8N1 in firmware).
- UART1 is enabled and I2C init did not collide with another I2C device.

### Stage 4: Scope the UART1 TX (Open-Drain Level Check)

1. Connect an oscilloscope to the WK2132 UART1 TX pin.
2. Trigger a `heater_request = 1` command (or send a start frame from a test loop).
3. Measure:
   - **Baud rate:** 4800 ± 5 % (208 µs per bit).
   - **Idle:** bus goes HIGH (3.3 V), stays there between frames.
   - **Start bit:** transitions LOW correctly.
   - **16-byte frame structure:** 8 bits data, 1 stop (no parity).
4. **Verify TX is open-drain or has a current-limiting buffer.** Do NOT measure a push-pull output here; it **will damage the 3.86 V bus**.

### Stage 5: Add Open-Drain Level-Shift Stage

1. Insert a **non-inverting open-drain buffer** (e.g., SN74LVC1G07, or a cascaded NPN emitter-follower) between WK2132 TX and the heater data line.
2. Verify on the scope:
   - Idle: ~3.86 V (measured at the heater connector).
   - Transmit: pulls LOW correctly to ~0.5 V.
   - Rise time: ~100 µs (acceptable for 4800 baud).
3. **Confirm the factory VEVOR display is physically disconnected.** Two active masters will collide.

### Stage 6: Connect the Heater (No Fuel)

1. Connect the heater data line to the STM32 bus (via the open-drain stage from Stage 5).
2. **Do not add fuel to the heater tank yet.**
3. Flash the firmware with `heater_request = 0` (stop).
4. Monitor Modbus register 55 (heater state) and 63 (age).
5. Within **10 seconds**, confirm:
   - A valid heater frame is received (age < 3000 ms).
   - State = 0 (off).
   - No checksum failures (reg 66 = 0).
6. Verify `safe_to_power_down` flag (reg 64 bit2) is set.

**Diagnostic:** If no frames are received, check:
- Data-line voltage idle ~3.86 V (is the bus really pulled up?).
- RX level-limiting is in place (WK2132 RX is not seeing overvoltage).
- Factory display is truly disconnected.
- Heater connector is properly seated.

### Stage 7: Fueled Level-1 Start and Full Cooldown (Operator Supervised)

1. Fill the heater tank with diesel fuel.
2. Set `heater_request = 1` and `heater_level = 1`.
3. Monitor Modbus registers live (via telemetry dashboard or serial monitor):
   - Reg 55 (state): 0 → 1 (preheat) → 2 (ignition) → 3 (running).
   - Reg 62 (state_seconds): counts up during each phase.
   - Reg 58 (supply_mv): should stabilize 24 V (24000 mV).
   - Reg 59 (fan_rpm): should increase once running.
4. After ~3–5 minutes at level 1, set `heater_request = 0`.
5. Monitor the **cooldown sequence** (~5 minutes):
   - State: 3 (running) → 4 (cooldown).
   - State_seconds: counts from 0 to ~300 s.
   - Confirm the heater fan runs and glow plug is off.
6. When state reaches 0 (off) and age is fresh, `safe_to_power_down` must be set.

**Safety notes:**
- **Operator must supervise the full startup and cooldown.**
- Stop the test immediately if:
  - Heater error code is set (reg 57 ≠ 0).
  - Supply voltage drops below 22 V (reg 58 < 22000 mV).
  - Fan is not spinning (reg 59 = 0 while running).
  - Comms fault flag is set continuously.
- If startup fails (never reaches running state), refer to the heater manufacturer manual for error-code meanings (reg 57).

---

## 7. Testing Done

### Host Suite (Unity + CMake, `firmware/Tests/`)

- **WK2132 driver backend seam** (Task 1):
  - Register read/write sequences over a fake I2C backend.
  - Baud-rate generator configuration verified.
  - No on-target I2C hardware required; all driven by unit tests.

- **VEVOR protocol + checksum verification** (Task 2):
  - **1221 captured heater frames** analyzed and re-encoded.
  - CRC-16 XMODEM checksum **0 failures** against captured data.
  - Start/stop/level frame encoding verified.
  - Telemetry frame parsing verified.

- **Heater FSM and safety flags** (Task 3):
  - State transitions (off → preheat → ignition → running → cooldown → off) verified against firmware state machine.
  - Safety-flag logic (fresh, cooldown, safe-to-power-down, comms-fault, transport-fault) unit-tested.
  - **Comms-loss scenario:** flag is set and never clears `safe_to_power_down` while active.
  - **Fresh state-0 test:** only a ≤3 s, checksum-valid state-0 response sets `safe_to_power_down`.

- **Modbus register block** (Task 4):
  - Registers 53–67 provide and update correctly.
  - `MB_REG_MAX` expansion from 52 to 70 verified (no off-by-one errors).
  - Flag bits in reg 64 set/clear correctly.

### On-Target (ARM cross-compile, CubeIDE)

- **I2C2 driver** (`drv_wk2132_i2c2.c`) compiles clean with no warnings.
- **UART-over-I2C sequences** are linked and callable (not tested without I2C hardware).
- **Firmware link includes all heater services** (`wk2132.c`, `vevor_heater.c`, `heater.c`, `mbp_heater.c`) with correct symbol resolution.

### Deferred (Hardware In Hand)

- **Real I2C2 initialization** and WK2132 register access (Stage 2).
- **UART loopback and framing** (Stage 3–4).
- **Bus-level verification** (open-drain, level-limiting) (Stage 5).
- **Heater integration** (Stages 6–7): valid frames, state transitions, safe-to-power-down logic on a running heater.
- **Fuel-system test:** startup, preheat/ignition timing, cooldown sequence with real combustion.

---

## Open Items (Flagged)

1. **Board revision power-rail reconciliation:**
   - R0: PC3 is **VCC_EN** (enable supply switch).
   - R1: PC3 is **LEDS_OFF** (LED inhibit).
   - Confirm which revision this firmware is targeting and reconcile the DFR0627 power source (switched 3P3_VCC or always-on).

2. **DFR0627 address DIP confirmation:**
   - Physical board must verify A1/A0 are both 1 (address 0x70).
   - If address is different, firmware init will fail.

3. **Exchanger-temperature conversion (future task):**
   - Register 61 (exchanger_raw) is exposed as a raw ADC value.
   - A future task will implement the conversion to °F or °C (Steinhart-Hart or manufacturer lookup table).

4. **DFR0627 temperature rating:**
   - Bridge is rated **85 °C maximum**; confirm this meets the final-product requirement (some automotive/industrial targets 125 °C).
   - If higher is needed, a higher-rated I2C-to-UART bridge must be sourced.

5. **Open-drain level-shift and power wiring:**
   - The non-inverting open-drain buffer between WK2132 TX and the heater bus is a **hardware task** (layout, PCB, component sourcing).
   - Power sourcing of the DFR0627 (confirmed 3P3_VCC available on this board revision) is also a hardware responsibility.
   - Firmware assumes both are correct; misbehavior on the heater bus (no frames, corruption, over-voltage) is a wiring issue.

