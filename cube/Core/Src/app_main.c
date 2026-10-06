#include "app_main.h"
#include "main.h"       /* CubeMX-generated HAL: HAL_GetTick() (SysTick 1 ms base) */
#include "sched.h"      /* portable M5 cooperative scheduler (App/services) */
#include "bsp_io.h"      /* portable relay/discrete-input service (App/services) */
#include "drv_bsp_io.h"  /* Task 3: concrete STM32G0 GPIO backend for bsp_io */
#include "bsp_pwm.h"     /* portable fan-PWM service (App/services) */
#include "drv_bsp_pwm.h" /* Task 4: concrete STM32G0 TIM2 backend for bsp_pwm */
#include "nvm.h"          /* portable NVM (params/journal) service (App/services) */
#include "drv_s25fl064.h" /* Task 6: concrete SPI2 NOR backend for nvm */
#include "rtc.h"          /* portable RTC/calendar service (App/services) */
#include "drv_mcp7940n.h" /* Task 7: concrete I2C1 MCP7940N backend for rtc */
#include "sensors.h"      /* portable sensor conversion service (App/services) */
#include "sensors_cal.h"  /* VREF_CAL_DEFAULT (scalar defines only without OWNER) */
#include "drv_bsp_adc.h"  /* Task 8: concrete ADC1+DMA feed for sensors */
#include "drv_rpm_tim3.h" /* Task 9: concrete TIM3_CH1 tach capture -> rpm_source_t */
#include "drv_heater_i2c2.h" /* I2C2 backend for the heater coprocessor (0x64) */
#include "mb_engine.h"       /* portable Modbus RTU engine (App/services) */
#include "mbp_sys.h"         /* portable system provider (fw-rev/reset regs) */
#include "mbp_nvm.h"         /* portable NVM Modbus provider (persisted regs) */
#include "mbp_rtc.h"         /* portable RTC/calendar Modbus provider (regs 24-31/42-48/52) */
#include "mbp_sensors.h"     /* portable sensors Modbus provider (regs 1/3/6/38/51) */
#include "control.h"         /* Task 11: control app init + 10ms/1s/1min slots (App/services) */
#include "drv_modbus_uart.h" /* Task 5: USART1 RS-485 RTU transport */
#include "boot_magic.h"      /* shared .noinit reset-magic word (g_boot_magic) */
#include "boot_decision.h"   /* BOOT_MAGIC_ENTER */
#include "mbp_boot.h"         /* reg-35 enter-bootloader Modbus command */
#include "app_confirm.h"      /* self-confirm active A/B slot when healthy */
#include "drv_iflash.h"       /* internal-flash backend (bootcfg store) */
#include "heater.h"          /* heater coprocessor client (App/services) */
#include "mbp_heater.h"      /* heater Modbus provider (regs 53-74) */
#include "sensors.h"         /* cabin (enclosure) temp -> heater ambient */

extern IWDG_HandleTypeDef hiwdg;  /* Task 10: CubeMX-generated (MX_IWDG_Init, ~2 s) */

/* Task 12: definition (once) of the app-side .noinit reset-magic word;
 * declared extern in boot_magic.h. Same section + physical address
 * (0x20023FE0, per every slot linker script) as the bootloader's own
 * g_boot_magic (boot/src/boot_main.c) — this is how the app hands the
 * bootloader a "please enter update mode" request across a reset. */
volatile uint32_t g_boot_magic __attribute__((section(".noinit")));

/* ── Board revision (PC3 differs between EF-G0B1R R0 and R1) ─────────────────
 * R0: PC3 = VCC_EN — drives a load switch (Q2) that powers the 3P3_VCC rail
 *     feeding the ADC VREF, the sensor/RPM analog front-end, the NOR flash and
 *     the RTC. It MUST be asserted high at boot before any of those are used,
 *     or they read dead (relays are on the always-on VCC_AO rail, so they work
 *     regardless). See "Schematic PDF_[No Variations].pdf" (EF-G0B1R R0).
 * R1: PC3 is repurposed as LEDS_OFF — do NOT drive it as VCC_EN.
 * Default is R0 (current bench + near-term units); build an R1 image with
 * -DBOARD_REV_R1 to leave PC3 alone. */
#if !defined(BOARD_REV_R0) && !defined(BOARD_REV_R1)
#define BOARD_REV_R0 1
#endif

#ifdef BOARD_REV_R0
/* Assert VCC_EN (PC3) to power the 3P3_VCC rail, then let it settle before any
 * NOR/RTC/ADC/RPM access. GPIOC clock is already on (MX_GPIO_Init); the enable
 * here is idempotent. */
static void board_r0_vcc_enable(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_3;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, GPIO_PIN_SET);   /* VCC_EN = 1 */
    HAL_Delay(10);   /* 3P3_VCC rise + settle before nvm/rtc/adc/rpm init below */
}
#endif

/* -------------------------------------------------------------------------
 * Task 2 — SysTick -> cooperative scheduler superloop.
 *
 * The portable M5 scheduler (sched.c, host-tested by test_sched) is now driven
 * by the real 1 ms SysTick: HAL maintains a millisecond counter (HAL_GetTick(),
 * incremented in SysTick_Handler via HAL_IncTick()), and each loop pass we feed
 * the elapsed milliseconds to sched_service(), then dispatch due slots with
 * sched_run().
 *
 * Task 11 registers the real control slots below (control_10ms_slot /
 * control_1s_slot / control_1min_slot). MCO on PA8 still validates the 64 MHz
 * clock from Task 1.
 * ---------------------------------------------------------------------- */

/* Task 10 — reg-34 reset action: full MCU reset via the Cortex-M0+ AIRCR.
 * Registered with mbp_sys so a Modbus write to reg 34 reboots the board. */
static void sys_reset(void)
{
    NVIC_SystemReset();   /* never returns */
}

/* Task 13 — reg-35 enter-bootloader command, wired via mbp_boot_register()
 * below. mbp_boot's wr_boot() calls engine_running() first (refusing while
 * it is non-zero) and only calls enter_bl() otherwise. */

/* Engine-running / engine-could-be-energized predicate. FIX (post-review):
 * ctx->engine_op_status alone is NOT enough — it is bound read-only to
 * Modbus reg 22 (control_io.c: rd_eng / mb_reg_bind(22, ...)) and is only
 * set ST_RUNNING at the very END of engine start (control_engine_start.c,
 * case ES_CHECK_PRESSURE, after oil pressure confirms good). During the
 * whole glow/fuel-prime/crank sequence BEFORE that (op_state ==
 * OP_ENGINE_START, sub_states ES_GLOWPLUG_ON..ES_ENGINE_ON, ~15-40 s with
 * glow_plug/fuel_pump/starter energized in turn) engine_op_status is still
 * ST_OFF, so the old (engine_op_status == ST_RUNNING)-only check would have
 * let a reg-35 write reset the MCU mid-crank/mid-glow.
 *
 * Checked every other op_state (control.h's control_op_state_t) against
 * every ctx->out.{fuel_pump,starter,glow_plug} write site (grepped across
 * App/services/control_*.c) for a second gap:
 *   - OP_POWER_UP (control_powerup.c) / OP_OFF (control_off.c): both force
 *     `ctx->out = (apu_outputs_t){0}` — never energize these outputs.
 *   - OP_CLIMATE / OP_BATTERY (control_climate.c / control_battery.c):
 *     never write fuel_pump/starter/glow_plug themselves; they are only
 *     reached via OP_ENGINE_START's ES_COOL_ON handoff, by which point
 *     engine_op_status is already ST_RUNNING — covered by the 2nd term
 *     below.
 *   - OP_ERROR_SHUTDOWN (control_error_shutdown.c): every case either calls
 *     control_deenergize_all() (which zeroes fuel_pump/starter/glow_plug
 *     AND resets engine_op_status/control_status to ST_OFF in the same
 *     call, control.c:44-50) or (ERR_AC_LOW/HIGH_PRESSURE) only clears
 *     compressor_clutch while leaving engine_op_status untouched (so if the
 *     engine was running, ST_RUNNING is still set and still caught below).
 *   - OP_COLD_STORAGE: never registered in control_app_init() (s_mode[] stays
 *     NULL) and no code ever sets ctx->op_state = OP_COLD_STORAGE — dead/
 *     unreachable state (matches project notes: cold-storage OI-6 deferred).
 *   - OP_DIAG (control_diag.c) — GENUINE SECOND GAP: the guarded
 *     technician relay-actuation path (reg 50 wr_diag_out ->
 *     diag_apply_active()) CAN set ctx->out.fuel_pump / .starter /
 *     .glow_plug = true directly (idx == OUT_FUEL_PUMP/OUT_STARTER/
 *     OUT_GLOW_PLUG) while op_state == OP_DIAG, and NEVER touches
 *     engine_op_status (stays ST_OFF the whole time) — so the
 *     engine_op_status-only check would have missed a technician actively
 *     energizing the starter/fuel pump/glow plug during a component test.
 *     diag_engine_gate_ok()/DIAG_ENGINE_TMR (4-5 s auto-cutoff) bound how
 *     long, but not whether, those outputs can be live — conservatively
 *     refuse for the whole OP_DIAG state (control_diag.c's own s_active is
 *     a private static with no exported per-output accessor, and this is a
 *     one-line-scope fix, not grounds to add one).
 *
 * control_app_ctx() is the existing public, app-global accessor
 * (App/services/control.h) — no new control-layer code needed. */
static int app_engine_running(void)
{
    const apu_ctx_t *ctx = control_app_ctx();
    return (ctx->op_state == OP_ENGINE_START)
        || (ctx->op_state == OP_DIAG)
        || (ctx->engine_op_status == ST_RUNNING);
}

/* Sets the reset-magic word and resets — the bootloader (boot_decision.c)
 * sees BOOT_MAGIC_ENTER on the next boot and enters update mode instead of
 * jumping to the active app slot. */
static void app_enter_bl(void)
{
    g_boot_magic = BOOT_MAGIC_ENTER;
    NVIC_SystemReset();   /* never returns */
}

/* Task 14 — health predicate for app_confirm_tick(), sampled once per 1 s
 * control slot (app_1s_slot() below). "Healthy" = the control state machine
 * is not mid/post error-shutdown: op_state != OP_ERROR_SHUTDOWN and
 * error_state == ERR_NONE (control_error_shutdown.c/control_off.c are the
 * only places error_state is ever cleared back to ERR_NONE).
 *
 * FIX (post-review): deliberately does NOT gate on
 * sensors_get_ext_state(). That reports the AN_Ext_Temp/PA4 coolant-sender
 * ADC channel's fault state (sensors.c: sensors_coolant_temp_f() sets
 * SENSOR_OFF on a short-to-ground) — a routine automotive sender
 * disconnect/short/open unrelated to firmware health. Gating self-confirm
 * on it means a disconnected coolant sender alone would block
 * app_confirm_tick() from ever accumulating a healthy window, the slot
 * would never reach SLOT_STATE_COMMITTED, and the bootloader would revert
 * perfectly good firmware after TRIAL_BOOT_LIMIT boots. App/services/
 * control_sample.c already made this exact call for glow-plug timing
 * ("The AN_Ext_Temp net (PA4) now carries the coolant sender... so
 * glow-plug cold-start timing keys off the on-board enclosure PTC" instead)
 * — same precedent applies here. Self-confirm only needs "the app booted
 * and ran cleanly without faulting"; a genuinely broken app crashes/hangs
 * and trips the IWDG/trial-counter, which is the real revert protection. */
static int app_is_healthy(void)
{
    const apu_ctx_t *ctx = control_app_ctx();
    return (ctx->op_state != OP_ERROR_SHUTDOWN)
        && (ctx->error_state == ERR_NONE);
}

/* Task 14 wrapper: run the real 1 s control slot, then feed its result into
 * the self-confirm health window. Registered on SLOT_1S below instead of
 * control_1s_slot directly, so all bootloader-facing wiring stays local to
 * app_main.c. */
static void app_1s_slot(void)
{
    control_1s_slot();
    app_confirm_tick(app_is_healthy());
}

void app_main(void)
{
    /* Task 10 — IWDG is already running (MX_IWDG_Init, ~2 s). In debug builds,
     * halt it when the core is stopped so breakpoints at the bench don't trip a
     * reset; production builds leave it free-running. */
#ifdef DEBUG
    __HAL_DBGMCU_FREEZE_IWDG();
#endif

#ifdef BOARD_REV_R0
    /* R0: power the 3P3_VCC rail (VCC_EN = PC3) BEFORE the nvm/rtc/adc/rpm init
     * below — on an R0 board the NOR flash, RTC, ADC VREF and sensor/RPM front-
     * end are all on that gated rail. (Not compiled on an R1 build.) */
    board_r0_vcc_enable();
#endif

    /* Task 3 — relay/discrete-input backend up FIRST, then force every output
     * OFF before anything (scheduler, control, Modbus) can command an actuator.
     * The engine, starter, glow-plug and compressor must never be driven by a
     * power-on glitch or an uninitialised GPIO. MX_GPIO_Init() has already set
     * each relay pin to its de-energized initial level; this is the explicit,
     * belt-and-suspenders safe-default-off through the portable API. */
    bsp_io_init(drv_bsp_io_backend());
    for (int o = 0; o < (int)OUT_COUNT; ++o) {
        bsp_out_set((bsp_out_t)o, false);
    }

    /* Task 4 — fan-PWM backend: start TIM2 CH1/CH2 at 0 % (evap + condenser fans
     * OFF) as part of the safe-default. */
    bsp_pwm_init(drv_bsp_pwm_backend());
    drv_bsp_pwm_start();

    /* Task 6 — NVM backend up before the Modbus provider that exposes persisted
     * registers, and before control (Task 11) so settings load at boot. nvm_init
     * reads the journal shadow and factory-inits a blank part. */
    nvm_init(drv_s25fl064_backend());

    /* Task 14 — self-confirm: load the A/B boot-config from internal flash
     * and arm the healthy-uptime confirm window if the active slot is still
     * TRIAL. Must run before app_1s_slot() (registered below) can call
     * app_confirm_tick(); drv_iflash_backend() needs no separate init (plain
     * HAL flash, always addressable once the core is running). */
    app_confirm_init(drv_iflash_backend());

    /* Task 7 — RTC backend (MCP7940N over I2C1). rtc_init only stashes the
     * backend; the rtc service starts the oscillator (ST) and enables battery
     * backup (VBATEN) on the first set-time. Bound before its Modbus provider. */
    rtc_init(drv_mcp7940n_backend());

    /* Heater coprocessor (EF-L011 / C011) on I2C2 at 0x64. Optional hardware:
     * heater_init only stashes the backend; an absent coprocessor is reported
     * as heater_present=0 and never blocks or faults boot. No nRST GPIO is
     * wired yet (spec open item 6), so no reset hook. */
    heater_init(drv_heater_i2c2_backend(), NULL, HAL_GetTick());

    /* Task 8 — sensor conversion service + ADC1 feed. sensors_init sets the
     * per-channel averaging windows (must run before any sample) and the VREF
     * trim; drv_bsp_adc_init calibrates ADC1 and starts the free-running scan +
     * circular DMA. Conversions are drained into sensors on SLOT_100MS below. */
    sensors_init(VREF_CAL_DEFAULT, /*temp_cal*/ 0);
    drv_bsp_adc_init();

    /* Task 9 — engine RPM from the tach pulse on TIM3_CH1 (PA6). Start the
     * input capture; drv_rpm_source() is handed to mbp_sensors_register below
     * so reg 38 reads live RPM (0 until the engine turns / after a stop). */
    drv_rpm_tim3_init();

#if BSP_IO_BENCH_RELAY_WALK
    drv_bsp_io_bench_relay_walk();     /* bench Step 4: click each relay in turn */
#endif

    /* Task 5 — RS-485 Modbus RTU slave. Bring the register engine up and register
     * every provider (bind order is irrelevant — the register model is a table):
     * sys (fw-rev/boot/reset), NVM (persisted settings), RTC, sensors. Control
     * regs are bound by control_app_init() below; reception starts after. */
    mb_engine_init();
    mbp_sys_register(sys_reset);      /* Task 10: reg-34 write -> NVIC_SystemReset */
    mbp_boot_register(app_engine_running, app_enter_bl); /* Task 13: reg-35 enter-bootloader */
    mbp_nvm_register();               /* persisted regs (runtime hrs, settings, flags) */
    mbp_rtc_register();               /* Task 7: calendar/RTCC/SRAM regs (24-31/42-48/52) */
    mbp_sensors_register(drv_rpm_source()); /* Task 8/9: temp/batt regs 1/3/6/51 + reg 38 live RPM */
    mbp_heater_register();            /* heater regs 53-74 (command + telemetry) */

    /* Task 11 — control application: init the shared apu_ctx, register the six
     * OP_* mode handlers, and bind the control registers (2,4,5,10,17,18,22,23,
     * 32,33,49,50). Runs after mb_engine_init (reg table) and before RX starts,
     * so every control register is bound before the master can query it. */
    control_app_init();

    drv_modbus_uart_init();

    sched_init();                     /* clears scheduler state + app_timers_init() */
    /* Task 11 — real control slots (replacing the Task-2 cadence probe):
     *   10 ms : sample sensors + inputs -> control_tick -> outputs_apply
     *   100 ms: drain ADC conversions into the sensors averager (Task 8)
     *   1 s   : compressor timers + climate/battery NVM settings sample
     *   1 min : runtime-hour accounting + oil-change checks */
    sched_register(SLOT_10MS,  control_10ms_slot);
    sched_register(SLOT_100MS, drv_bsp_adc_drain);
    sched_register(SLOT_1S,    app_1s_slot);   /* Task 14: control_1s_slot() + app_confirm_tick() */
    sched_register(SLOT_1MIN,  control_1min_slot);

    /* Task 10 (bench fix) — arm the IWDG HERE, after all boot init has run
     * (relays/PWM/NVM/RTC/sensors/RPM/Modbus/control) and immediately before the
     * steady-state superloop. The watchdog guards the running loop, not the
     * one-time boot: nvm_init()'s journal scan can exceed the ~2 s IWDG period,
     * so arming it in main() (before app_main) caused a boot reset loop. Config
     * mirrors the generated MX_IWDG_Init() (PRESC 32, reload 1999 -> ~2 s); the
     * MX_IWDG_Init() call in main() is disabled — see Core/Src/main.c. */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Window    = 4095;
    hiwdg.Init.Reload    = 1999;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        Error_Handler();
    }

    uint32_t last = HAL_GetTick();
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        uint16_t dt  = (uint16_t)(now - last);   /* elapsed ms since last pass */
        if (dt != 0u)
        {
            last = now;
            sched_service(dt);   /* advance the 1 ms time base + set due flags */
            sched_run();         /* dispatch due slots to their handlers */
        }
        drv_modbus_uart_poll();  /* drain any assembled RTU frame -> engine -> DMA TX */
        heater_set_ambient_f(sensors_get_encl_temp_f()); /* AUTOTERM room temp (enclosure stand-in) */
        heater_process(now);     /* coprocessor client: self-paced at 250 ms */
        HAL_IWDG_Refresh(&hiwdg);/* Task 10: kick the watchdog once per superloop pass */
    }
}
