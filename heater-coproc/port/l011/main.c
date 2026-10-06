/* EF-L011 bring-up stage (Plan 2 Task 1): clock, safe GPIO, tick, IWDG.
   Replaced by the full superloop in Task 2. */
#include "port.h"

int main(void)
{
    port_clock_init();
    port_gpio_init();                 /* safe levels before anything else */
    port_tick_init();
    port_iwdg_start();
    for (;;) {
        port_iwdg_kick();
    }
}
