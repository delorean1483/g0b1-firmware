/* system_boot.c — trimmed CMSIS system layer for the bootloader.
 *
 * Replaces cube/Core/Src/system_stm32g0xx.c. SystemInit() does NOT write
 * SCB->VTOR: the bootloader runs from 0x08000000 where the boot vector table
 * already lives, so the reset-time vector table is already correct. Provides
 * the CMSIS globals (SystemCoreClock + prescaler tables) that the HAL links
 * against, and SystemCoreClockUpdate() for completeness.
 */
#include "stm32g0xx.h"

#if !defined(HSE_VALUE)
#define HSE_VALUE (8000000UL)
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE (16000000UL)
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE (32000UL)
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE (32768UL)
#endif

uint32_t SystemCoreClock = 16000000UL;

const uint32_t AHBPrescTable[16UL] = {0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 1UL, 2UL, 3UL, 4UL, 6UL, 7UL, 8UL, 9UL};
const uint32_t APBPrescTable[8UL] = {0UL, 0UL, 0UL, 0UL, 1UL, 2UL, 3UL, 4UL};

void SystemInit(void)
{
  /* No vector-table relocation: the boot vector table is already at
     0x08000000 (the reset base). */
}

void SystemCoreClockUpdate(void)
{
  uint32_t tmp;
  uint32_t pllvco;
  uint32_t pllr;
  uint32_t pllsource;
  uint32_t pllm;
  uint32_t hsidiv;

  switch (RCC->CFGR & RCC_CFGR_SWS)
  {
    case RCC_CFGR_SWS_0: /* HSE */
      SystemCoreClock = HSE_VALUE;
      break;
    case (RCC_CFGR_SWS_1 | RCC_CFGR_SWS_0): /* LSI */
      SystemCoreClock = LSI_VALUE;
      break;
    case RCC_CFGR_SWS_2: /* LSE */
      SystemCoreClock = LSE_VALUE;
      break;
    case RCC_CFGR_SWS_1: /* PLL */
      pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC);
      pllm = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLM) >> RCC_PLLCFGR_PLLM_Pos) + 1UL;
      if (pllsource == 0x03UL)
        pllvco = (HSE_VALUE / pllm);
      else
        pllvco = (HSI_VALUE / pllm);
      pllvco = pllvco * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos);
      pllr = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLR) >> RCC_PLLCFGR_PLLR_Pos) + 1UL);
      SystemCoreClock = pllvco / pllr;
      break;
    case 0x00000000U:
    default: /* HSI */
      hsidiv = (1UL << ((READ_BIT(RCC->CR, RCC_CR_HSIDIV)) >> RCC_CR_HSIDIV_Pos));
      SystemCoreClock = (HSI_VALUE / hsidiv);
      break;
  }
  tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos)];
  SystemCoreClock >>= tmp;
}
