#include "app_main.h"
#include "main.h"    /* CubeMX-generated HAL: HAL_GetTick() (SysTick 1 ms base) */
#include "sched.h"   /* portable M5 cooperative scheduler (App/services) */

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
    sched_init();                     /* clears scheduler state + app_timers_init() */
    sched_register(SLOT_1S, on_1s);   /* temporary cadence probe (removed in Task 3) */

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
    }
}
