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
#include "mb_engine.h"       /* portable Modbus RTU engine (App/services) */
#include "mbp_sys.h"         /* portable system provider (fw-rev/reset regs) */
#include "mbp_nvm.h"         /* portable NVM Modbus provider (persisted regs) */
#include "mbp_rtc.h"         /* portable RTC/calendar Modbus provider (regs 24-31/42-48/52) */
#include "mbp_sensors.h"     /* portable sensors Modbus provider (regs 1/3/6/38/51) */
#include "control.h"         /* Task 11: control app init + 10ms/1s/1min slots (App/services) */
#include "drv_modbus_uart.h" /* Task 5: USART1 RS-485 RTU transport */

extern IWDG_HandleTypeDef hiwdg;  /* Task 10: CubeMX-generated (MX_IWDG_Init, ~2 s) */

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

    /* Task 7 — RTC backend (MCP7940N over I2C1). rtc_init only stashes the
     * backend; the rtc service starts the oscillator (ST) and enables battery
     * backup (VBATEN) on the first set-time. Bound before its Modbus provider. */
    rtc_init(drv_mcp7940n_backend());

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
    mbp_nvm_register();               /* persisted regs (runtime hrs, settings, flags) */
    mbp_rtc_register();               /* Task 7: calendar/RTCC/SRAM regs (24-31/42-48/52) */
    mbp_sensors_register(drv_rpm_source()); /* Task 8/9: temp/batt regs 1/3/6/51 + reg 38 live RPM */

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
    sched_register(SLOT_1S,    control_1s_slot);
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
        HAL_IWDG_Refresh(&hiwdg);/* Task 10: kick the watchdog once per superloop pass */
    }
}
