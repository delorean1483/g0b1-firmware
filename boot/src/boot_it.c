/* boot_it.c — the one interrupt the bootloader needs.
 *
 * HAL's default time base is SysTick (installed by HAL_InitTick). HAL_Delay()
 * and every HAL blocking timeout (UART receive/transmit) depend on HAL_GetTick()
 * advancing, so SysTick_Handler must call HAL_IncTick(). Every other vector is
 * left weak-aliased to the startup Default_Handler. */
#include "stm32g0xx_hal.h"

void SysTick_Handler(void)
{
  HAL_IncTick();
}
