# Phase B — SWD dry‑run: validate op‑state → actuator control

**Purpose:** prove the firmware drives the *correct output, with the correct polarity/timing,* for every op‑state — **before** any real APU loads are connected. Runs today on the **standalone bench board** over SWD (ST‑Link on HDR1); no RS‑485 converter, no real unit needed. This retires the "never driven real actuators" risk safely.

> ⚠️ **Safety:** do this with **relays/loads disconnected** (or ULN2003 outputs open / dummy LEDs). Level 1 pokes individual outputs; Level 2 walks the state machine (it *will* command starter/fuel/glow/compressor). Keep loads off until Level 1 + Level 2 pass, then progressive live‑load is a later step (see milestone plan Phase B step 3).

---

## 0. Setup
- **Build/flash the DEBUG configuration** (CubeIDE). Debug build freezes the IWDG while halted (`__HAL_DBGMCU_FREEZE_IWDG`), so breakpoints don't trigger a watchdog reset. (A Release build **will** reset ~2 s after any halt.)
- Attach ST‑Link → HDR1, start a debug session (CubeIDE **Debug**, or `openocd -f interface/stlink.cfg -f target/stm32g0x.cfg` + `arm-none-eabi-gdb`).
- Global context symbol: **`s_ctx`** (file‑static in `App/services/control_app.c`; also reachable via `control_app_ctx()`). Add it to **Live Expressions** (CubeIDE) or use `p s_ctx` (GDB). Expand `s_ctx.out` to see the requested outputs.

## Reference — pins & enums

**Relay outputs** (active‑HIGH at the MCU → ULN2003 sinks the coil; **MCU pin HIGH = actuator commanded ON**):

| `s_ctx.out` field | `OUT_` enum | MCU pin | Signal |
|---|---|---|---|
| `fuel_pump` | OUT_FUEL_PUMP | **PC12** | Fl_Pmp_Snoid |
| `starter` | OUT_STARTER | **PC11** | Sttr_Snoid |
| `glow_plug` | OUT_GLOW_PLUG | **PB8** | Glow_Plug |
| `compressor_clutch` | OUT_COMPRESSOR_CLUTCH | **PB5** | Cmprssr_Clutch |
| `heat_reverse` | OUT_HEAT_REVERSER | **PB4** | Heat_Reverser (OI‑1) |
| `evap_fan` | OUT_EVAP_FAN | **PC10** | Evap_Fan |
| `condenser_fan` | OUT_CONDENSER_FAN | **PB9** | Condenser_Fan |

**Fan PWM** (TIM2, ~1 kHz): `PWM_EVAP_FAN` = **PC4** (TIM2_CH1), `PWM_CONDENSER_FAN` = **PC5** (TIM2_CH2). Duty in permille (0..1000). Read back: `p ((TIM_TypeDef*)TIM2)->CCR1` / `CCR2`, or `s_duty[]` in drv_bsp_pwm.c, or scope PC4/PC5.

**Discrete inputs:** `IN_OIL_PRESSURE` = **PD6** (HIGH = good — *polarity bench‑confirm pending*), `IN_TRUCK_IGNITION` = **PD2** (LOW = present, active‑low). 500 ms debounce.

**Mode command** `s_ctx.mode_request` (`op_mode_t`): `0`=off, `1`=climate, `2`=battery.
**Observed state** `s_ctx.op_state` (`control_op_state_t`): 0 POWER_UP · 1 OFF · 2 ENGINE_START · 3 CLIMATE · 4 BATTERY · 5 COLD_STORAGE · 6 ERROR_SHUTDOWN. Also watch `control_status`, `engine_op_status`, `error_state`, `sub_state`.

---

## Level 1 — Output‑drive & polarity check (deterministic, safest)
Verifies each pin toggles and the polarity is right, isolated from the FSM.

**Method A — GDB poke (fastest).** Halt, then per output:
```gdb
call bsp_out_set(OUT_FUEL_PUMP, 1)     # drive it
# measure PC12: expect HIGH (~3.3 V) ; and:
p/x ((GPIO_TypeDef*)GPIOC)->ODR        # bit 12 should be 1
call bsp_out_set(OUT_FUEL_PUMP, 0)     # expect PC12 LOW, bit clears
```
**Method B — CubeIDE (no function‑call).** Set a breakpoint on the `outputs_apply(&s_ctx);` line in `control_10ms_slot()`. When it hits, set `s_ctx.op_state = OP_OFF` and the single field under test (e.g. `s_ctx.out.starter = 1`), then **Step Over** `outputs_apply`. Measure the pin; **Resume** to the next hit for the next output.

Fill in — measure each pin HIGH when set, LOW when cleared:

| Output | Pin | HIGH when ON? | LOW when OFF? |
|---|---|---|---|
| fuel_pump | PC12 | ☐ | ☐ |
| starter | PC11 | ☐ | ☐ |
| glow_plug | PB8 | ☐ | ☐ |
| compressor_clutch | PB5 | ☐ | ☐ |
| heat_reverse | PB4 | ☐ | ☐ |
| evap_fan | PC10 | ☐ | ☐ |
| condenser_fan | PB9 | ☐ | ☐ |

**PWM:** `call bsp_pwm_set(PWM_EVAP_FAN, 500)` → PC4 should show ~50 % duty (scope) and `CCR1 == (ARR+1)/2`. Repeat 0 / 1000 (PC4), and PWM_CONDENSER_FAN on PC5.

✅ **Level 1 pass:** every pin follows its command with the expected polarity; PWM duty tracks permille.

---

## Level 2 — FSM walk per op‑state (mode → op_state → outputs)
Confirms the control logic drives the right *set* of outputs per state on real silicon, and that inputs/faults behave. The FSM needs inputs to progress; fake them by **jumpering the physical pins** (debounced 500 ms) or by patching at a breakpoint after `control_inputs_service`.

Fake inputs (bench):
- **Ignition present:** jumper **PD2 → GND** (active‑low).
- **Oil pressure good:** jumper **PD6 → 3V3** (HIGH=good — *this step also validates that polarity assumption; if "good" reads as fault, the `active_high` flag needs flipping*).
- **Engine RPM:** inject a 3.3 V square wave into the tach (**PA6 / TIM3_CH1**) — `rpm ≈ freq × 6` (e.g. ~300 Hz ≈ 1800 rpm). No generator? Patch the RPM at a breakpoint / override the rpm‑source read in GDB.

Then, per mode, set `s_ctx.mode_request`, **Resume**, and check `op_state` + `s_ctx.out`:

| `mode_request` | Expect `op_state` progression | Expect outputs (when settled) |
|---|---|---|
| `0` off | → **OFF** | all relays OFF, fans off |
| `2` battery (ign+RPM faked) | POWER_UP → ENGINE_START → **BATTERY** | fuel on, starter pulses during start, then charging; starter off once "running" |
| `1` climate (ign+RPM faked) | POWER_UP → ENGINE_START → **CLIMATE** | fuel on; **compressor_clutch** on; **evap+condenser fans** on with OI‑2 condenser ramp on PC5 |

Fault/edge checks (high value):
- **No RPM (don't inject tach):** ENGINE_START should retry then raise `error_state = ERR_STARTING_FAILURE (6)` and fall to a safe state — confirm it doesn't hang with starter engaged.
- **Ignition removed (PD2 open):** system should leave the run states and return toward OFF/standby.
- **Glow‑plug timing:** in ENGINE_START at cold `ext_temp`, glow_plug (PB8) should assert for the glow interval before starter — check sequencing/timing.

---

## Critical safety checks (must pass)
- **Safe‑off:** in OFF, and after `control_deenergize_all(&s_ctx)`, **every** output pin reads LOW (PC12/PC11/PB8/PB5/PB4/PC10/PB9 all low, PWM 0). Verify from a *fresh boot* too (power‑on default = safe).
- **Error‑shutdown de‑energizes:** force an error path (e.g. ENGINE_START with no RPM → ERR_STARTING_FAILURE) and confirm ERROR_SHUTDOWN drops all outputs.
- **Starter never latches:** starter (PC11) must never stay asserted after "running" or after a failed start.

---

## Sign‑off
- ☐ Level 1: all 7 relays + 2 PWM channels correct polarity/duty
- ☐ Oil‑pressure input polarity confirmed (PD6) — flip `active_high` in `drv_bsp_io.c` if inverted
- ☐ Level 2: OFF / BATTERY / CLIMATE reach the right op_state with the right outputs
- ☐ Failed‑start → ERR_STARTING_FAILURE, safe fallback, starter released
- ☐ Safe‑off + error‑shutdown drop all outputs; power‑on default is safe

Passing this ⇒ firmware actuator control is trusted for progressive live‑load on a real unit (milestone Phase B step 3).
