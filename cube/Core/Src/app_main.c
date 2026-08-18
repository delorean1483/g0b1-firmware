#include "app_main.h"
#include "main.h"   /* CubeMX-generated: HAL + peripheral handles */

/* -------------------------------------------------------------------------
 * Task 1 — Bench bring-up validation via MCO (Master Clock Output).
 *
 * The EF-G0B1R has no simple MCU-driven LED (the RGBW status LED LD1 is
 * serial-controlled; the other LEDs are hardware status), so Task-1's
 * "it's alive + the clock is right" proof is the Master Clock Output on PA8:
 *
 *     MCO = SYSCLK / 16 = 64 MHz / 16 = 4 MHz
 *
 * Scope PA8 -> a clean 4 MHz square wave confirms the flash path, boot, and
 * the whole 64 MHz clock tree in one shot. MCO runs entirely in hardware
 * (configured in the .ioc), so no GPIO toggling is needed here.
 *
 * app_main() just idles until Task 2 replaces this loop with the cooperative
 * scheduler superloop (sched_service()/sched_run()).
 * ---------------------------------------------------------------------- */
void app_main(void)
{
    for (;;)
    {
        /* Clock is validated in hardware via MCO on PA8. Nothing to do yet;
         * Task 2 fills this loop with the scheduler. */
    }
}
