/* Clock, SysTick, IWDG, critical sections. */
#include "port.h"
#include "stm32l0xx.h"

static volatile uint32_t s_ms;

void port_clock_init(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {}
    /* 16 MHz in the reset voltage range needs 1 flash wait state. */
    FLASH->ACR |= FLASH_ACR_LATENCY;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) == 0u) {}
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_SW_HSI | RCC_CFGR_PPRE1_DIV2;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
    SystemCoreClock = 16000000u;
}

void port_tick_init(void)
{
    (void)SysTick_Config(16000u);
}

void SysTick_Handler(void) { s_ms++; }
uint32_t port_now(void) { return s_ms; }

void port_iwdg_start(void)
{
#ifdef DEBUG
    RCC->APB2ENR |= RCC_APB2ENR_DBGMCUEN;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
#endif
    IWDG->KR = 0xCCCCu;            /* start (LSI ~37 kHz) */
    IWDG->KR = 0x5555u;            /* unlock PR/RLR */
    IWDG->PR = 3u;                 /* /32 -> ~1.16 kHz */
    IWDG->RLR = 2000u;             /* ~1.7 s */
    while (IWDG->SR != 0u) {}
    IWDG->KR = 0xAAAAu;
}

void port_iwdg_kick(void) { IWDG->KR = 0xAAAAu; }
void port_lock(void)   { __disable_irq(); }
void port_unlock(void) { __enable_irq(); }

/* -nostartfiles drops crt0 (whose exit() drags in newlib stdio); the startup
   code still calls __libc_init_array, which needs these hooks. */
void _init(void) {}
void _fini(void) {}
