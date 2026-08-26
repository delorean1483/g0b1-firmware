# Phase B — Level 2 SWD: op-state → actuator FSM walk

**Purpose:** drive the control state machine through a full **CLIMATE** start and confirm the *right relays fire in the right order* on real silicon — the engine-start sequence and cooling handoff — before any real loads are connected. Runs on the standalone R0 bench board over SWD. No RS-485, no signal generator, **no RPM signal** (RPM is telemetry-only reg 38; it does not gate the FSM — oil pressure does), and **no input jumpers**.

> ⚠️ **Safety:** relays/loads disconnected (or dummy LEDs). This walks the real start sequence — glow, fuel, starter, compressor, fans will assert their GPIOs. Safe only with no actuators wired.

## Preconditions (already true on your bench)
- **Oil pressure reads OK** (`oil_pressure_ok = yes`) — this is the run-detect gate; the engine is declared RUNNING when oil is OK after cranking.
- **Ignition OFF** (`ignition = off`) — REQUIRED. Ignition *present* → `ERR_STANDBY` → shutdown (a truck APU must not run while the truck engine runs). Do **not** jumper PD2.
- DEBUG build (IWDG frozen while halted). VCC_EN/PC3 confirmed (sensors live).

## Reaching the control context in GDB (Debugger Console)
The shared context is `s_ctx` (type `apu_ctx_t`) in `control_app.c`:
```gdb
# primary (symbolic, survives rebuilds):
set var 'control_app.c'::s_ctx.mode_request = 1
# if that syntax is rejected, use the address (RE-CHECK after any rebuild):
#   p &'control_app.c'::s_ctx        -> prints the current address
#   set var ((apu_ctx_t*)0x20000078)->mode_request = 1
```
`mode_request`: `0`=off, `1`=climate, `2`=battery. It is not sensor-sampled, so the write holds until the FSM (or a Modbus reg-10 write) changes it.

## Watch expressions (read these at each snapshot)
```gdb
p 'control_app.c'::s_ctx.op_state          # 0 POWER_UP 1 OFF 2 ENGINE_START 3 CLIMATE 4 BATTERY 6 ERR_SHUTDOWN
p 'control_app.c'::s_ctx.sub_state
p 'control_app.c'::s_ctx.control_status     # 0 OFF 1 WARMING_UP 2 STARTING 3 RUNNING 4 DEFROST 5 CHARGING 6 COOLING 7 CHILLIN
p 'control_app.c'::s_ctx.error_state        # expect 0 (none) throughout
p 'control_app.c'::s_ctx.out                # the whole requested-output struct (fuel_pump/starter/glow_plug/compressor_clutch/heat_reverse/evap_fan/condenser_fan)
x/1xw 0x50000814                            # GPIOC ODR (physical) — baseline 0x00C (PC2 NOR-CS + PC3 VCC_EN)
x/1xw 0x50000414                            # GPIOB ODR (physical) — baseline 0x000
```
Relay bit map: **GPIOC** fuel PC12=0x1000, starter PC11=0x0800, evap PC10=0x0400 · **GPIOB** glow PB8=0x0100, compressor PB5=0x0020, heat-rev PB4=0x0010, condenser PB9=0x0200.

## Procedure
1. Enter debug, **Resume** so `app_main` runs (VCC_EN + all init), then **Suspend**.
2. Confirm the start conditions:
   ```gdb
   p 'control_app.c'::s_ctx.in_oil_pressure_ok    # expect true (1)
   p 'control_app.c'::s_ctx.in_truck_ignition     # expect false (0)
   ```
3. Command CLIMATE, then **Resume** (let the FSM free-run):
   ```gdb
   set var 'control_app.c'::s_ctx.mode_request = 1
   ```
   (Resume = F8. The 10 ms slot advances the FSM only while running.)
4. **Snapshot** by Suspend → read the watch expressions → Resume. Repeat every ~5–10 s to catch each phase. **Note:** if the external-temp sensor reads OFF, the glow phase is ~28 s, so the full run to compressor-on is ~40–45 s. Be patient between snapshots.

## Expected sequence (ext-temp sensor OFF ⇒ ~28 s glow)

| ~t | op_state | control_status | outputs asserted | GPIOC / GPIOB |
|---|---|---|---|---|
| 0 s | CLIMATE→ENGINE_START | OFF→WARMING_UP | **glow_plug** | GPIOB 0x100 |
| ~29 s | ENGINE_START | WARMING_UP | glow off, **fuel_pump** on | GPIOC 0x100C (fuel) |
| ~30 s | ENGINE_START | STARTING | fuel + **starter** | GPIOC 0x180C |
| ~34 s | ENGINE_START | STARTING | starter off, brief post-glow | GPIOC 0x100C |
| ~44 s | ENGINE_START→CLIMATE | RUNNING→COOLING | fuel (engine running) | GPIOC 0x100C |
| ~45 s+ | CLIMATE | COOLING | fuel + **compressor** + **condenser** + **evap** | GPIOC 0x140C / GPIOB 0x220 |

Steady cooling end-state: **GPIOC ≈ 0x140C** (fuel PC12 + evap PC10 + baseline), **GPIOB ≈ 0x220** (compressor PB5 + condenser PB9). Fan PWMs: evap `CCR1` (0x40000034) and condenser `CCR2` (0x40000038) non-zero (condenser stub = 1000‰ → full).

## Checks that matter
- **Order is correct:** glow → fuel → starter → (starter releases) → running → compressor → fans. Starter must **release** (PC11 back to 0) before/at RUNNING — it must never latch on.
- **error_state stays 0** through a clean start.
- **BATTERY path (optional):** repeat with `mode_request = 2` — expect the same start, then charging (no compressor/evap).
- **Return to safe-off:** `set var 'control_app.c'::s_ctx.mode_request = 0`, Resume → all outputs drop (OP_OFF); confirm GPIOC→0x00C, GPIOB→0x000.
- **Error-shutdown (optional, adversarial):** while running, force `set var 'control_app.c'::s_ctx.in_oil_pressure_ok = 0` — but note the 10 ms `control_inputs_service` re-samples the real pin each tick, so to hold it low you must actually open the oil input; expect `ERR_LOW_OIL` → OP_ERROR_SHUTDOWN → outputs drop.

## Sign-off
- ☐ Start sequence fires in correct order (glow→fuel→starter→running→compressor→fans)
- ☐ Starter releases at RUNNING (never latched)
- ☐ error_state == 0 through a clean CLIMATE start
- ☐ BATTERY mode reaches charging without compressor
- ☐ mode_request=0 returns all outputs to safe-off
- ☐ (optional) oil-loss forces ERROR_SHUTDOWN + safe-off

Passing this ⇒ the full firmware control path (mode → op-state → sequenced actuation → safe fallback) is validated on real silicon, clearing the way for progressive live-load on a real unit (milestone Phase B step 3 / Phase D).
