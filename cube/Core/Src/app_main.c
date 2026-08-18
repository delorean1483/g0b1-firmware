#include "app_main.h"
#include "main.h"       /* CubeMX-generated HAL: HAL_GetTick() (SysTick 1 ms base) */
#include "sched.h"      /* portable M5 cooperative scheduler (App/services) */
#include "bsp_io.h"      /* portable relay/discrete-input service (App/services) */
#include "drv_bsp_io.h"  /* Task 3: concrete STM32G0 GPIO backend for bsp_io */
#include "bsp_pwm.h"     /* portable fan-PWM service (App/services) */
#include "drv_bsp_pwm.h" /* Task 4: concrete STM32G0 TIM2 backend for bsp_pwm */
#include "nvm.h"          /* portable NVM (params/journal) service (App/services) */
#include "drv_s25fl064.h" /* Task 6: concrete SPI2 NOR backend for nvm */
#include "mb_engine.h"       /* portable Modbus RTU engine (App/services) */
#include "mbp_sys.h"         /* portable system provider (fw-rev/reset regs) */
#include "mbp_nvm.h"         /* portable NVM Modbus provider (persisted regs) */
#include "drv_modbus_uart.h" /* Task 5: USART1 RS-485 RTU transport */

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

void app_main(void)
{
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

#if BSP_IO_BENCH_RELAY_WALK
    drv_bsp_io_bench_relay_walk();     /* bench Step 4: click each relay in turn */
#endif

    /* Task 5 — RS-485 Modbus RTU slave. Bring the register engine up and register
     * the providers whose backends already exist (bind order is irrelevant — the
     * register model is a table). Only mbp_sys (fw-rev/boot-flag; reset callback
     * lands in Task 10) is bindable now; nvm/rtc/sensor providers join as their
     * backends come online (Tasks 6-8). Then start reception. */
    mb_engine_init();
    mbp_sys_register(NULL);           /* NULL reset fn: wr_reset no-ops until Task 10 */
    mbp_nvm_register();               /* persisted regs (runtime hrs, settings, flags) */
    drv_modbus_uart_init();

    sched_init();                     /* clears scheduler state + app_timers_init() */
    sched_register(SLOT_1S, on_1s);   /* temporary cadence probe (real control slots land in Task 11) */

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
    }
}
