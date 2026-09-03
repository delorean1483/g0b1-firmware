/* boot_jump.c — hand control to an application slot.
 *
 * Return the clocks/peripherals to a reset-like state, relocate the vector
 * table to the slot base (SCB->VTOR), load the slot's initial MSP, and branch
 * to its reset vector. Never returns. */
#include "boot_jump.h"
#include "stm32g0xx_hal.h"

void boot_jump_to_slot(uint32_t slot_base)
{
  uint32_t sp = *(volatile uint32_t *)(slot_base + 0u);
  uint32_t pc = *(volatile uint32_t *)(slot_base + 4u);

  __disable_irq();
  HAL_RCC_DeInit();                 /* return clocks to reset so the app re-inits cleanly */
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  /* Cortex-M0+ has a single NVIC ICER/ICPR register (max 32 IRQs; the G0B1
     uses IRQ 0..30). Disable and clear every pending external interrupt. */
  NVIC->ICER[0] = 0xFFFFFFFFu;
  NVIC->ICPR[0] = 0xFFFFFFFFu;
  SCB->VTOR = slot_base;
  __set_MSP(sp);
  __enable_irq();
  ((void (*)(void))pc)();           /* never returns */
}
