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
#include "drv_modbus_uart.h" /* Task 5: USART1 RS-485 RTU transport */

extern IWDG_HandleTypeDef hiwdg;  /* Task 10: CubeMX-generated (MX_IWDG_Init, ~2 s) */

/* -------------------------------------------------------------------------
 * Task 2 — SysTick -> cooperative scheduler superloop.
 *
 * The portable M5 scheduler (sched.c, host-tested by test_sched) is now driven
 * by the real 1 ms SysTick: HAL maintains a millisecond counter (HAL_GetTick(),
 * incremented in SysTick_Handler via HAL_IncTick()), and each loop pass we feed
 * the elapsed milliseconds to sched_service(), then dispatch due slots with
 * sched_run().
 *
 * Task-2 cadence proof (no MCU LED on this board): a SLOT_1S handler bumps a
 * global counter once per second. At the bench, put a watch on
 * `g_sched_1s_ticks` in the debugger -> it should advance ~1 per second,
 * confirming the SysTick-driven scheduler runs at the right cadence. (MCO on
 * PA8 still validates the 64 MHz clock from Task 1.)
 *
 * Task 3+ register the real control slots (control_10ms_slot / _1s_slot /
 * _1min_slot) here once their bsp/drv backends come online.
 * ---------------------------------------------------------------------- */

volatile uint32_t g_sched_1s_ticks = 0;   /* bench debug watch: ~1/sec */

static void on_1s(void)
{
    g_sched_1s_ticks++;
}

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
     * the providers whose backends already exist (bind order is irrelevant — the
     * register model is a table). Only mbp_sys (fw-rev/boot-flag; reset callback
     * lands in Task 10) is bindable now; nvm/rtc/sensor providers join as their
     * backends come online (Tasks 6-8). Then start reception. */
    mb_engine_init();
    mbp_sys_register(sys_reset);      /* Task 10: reg-34 write -> NVIC_SystemReset */
    mbp_nvm_register();               /* persisted regs (runtime hrs, settings, flags) */
    mbp_rtc_register();               /* Task 7: calendar/RTCC/SRAM regs (24-31/42-48/52) */
    mbp_sensors_register(drv_rpm_source()); /* Task 8/9: temp/batt regs 1/3/6/51 + reg 38 live RPM */
    drv_modbus_uart_init();

    sched_init();                     /* clears scheduler state + app_timers_init() */
    sched_register(SLOT_1S, on_1s);   /* temporary cadence probe (real control slots land in Task 11) */
    sched_register(SLOT_100MS, drv_bsp_adc_drain); /* Task 8: push ADC counts -> sensors averager */

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
