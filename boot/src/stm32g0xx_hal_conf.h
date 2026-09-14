/**
  ******************************************************************************
  * @file    stm32g0xx_hal_conf.h  (bootloader)
  * @brief   HAL configuration for the g0b1 bootloader. Only the modules the
  *          boot program links are enabled: FLASH, GPIO, UART, RCC, CORTEX,
  *          IWDG, PWR, and DMA. (DMA is required because the UART handle type
  *          embeds DMA_HandleTypeDef* — stm32g0xx_hal_uart.h will not compile
  *          without stm32g0xx_hal_dma.h — even though the boot UART is blocking
  *          and never starts a DMA transfer.)
  ******************************************************************************
  */
#ifndef STM32G0xx_HAL_CONF_H
#define STM32G0xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED

/* ########################## Register Callbacks selection ################## */
#define USE_HAL_ADC_REGISTER_CALLBACKS    0u
#define USE_HAL_CEC_REGISTER_CALLBACKS    0u
#define USE_HAL_COMP_REGISTER_CALLBACKS   0u
#define USE_HAL_CRYP_REGISTER_CALLBACKS   0u
#define USE_HAL_DAC_REGISTER_CALLBACKS    0u
#define USE_HAL_FDCAN_REGISTER_CALLBACKS  0u
#define USE_HAL_HCD_REGISTER_CALLBACKS    0u
#define USE_HAL_I2C_REGISTER_CALLBACKS    0u
#define USE_HAL_I2S_REGISTER_CALLBACKS    0u
#define USE_HAL_IRDA_REGISTER_CALLBACKS   0u
#define USE_HAL_LPTIM_REGISTER_CALLBACKS  0u
#define USE_HAL_PCD_REGISTER_CALLBACKS    0u
#define USE_HAL_RNG_REGISTER_CALLBACKS    0u
#define USE_HAL_RTC_REGISTER_CALLBACKS    0u
#define USE_HAL_SMBUS_REGISTER_CALLBACKS  0u
#define USE_HAL_SPI_REGISTER_CALLBACKS    0u
#define USE_HAL_TIM_REGISTER_CALLBACKS    0u
#define USE_HAL_UART_REGISTER_CALLBACKS   0u
#define USE_HAL_USART_REGISTER_CALLBACKS  0u
#define USE_HAL_WWDG_REGISTER_CALLBACKS   0u

/* ########################## Oscillator Values adaptation ################## */
#if !defined(HSE_VALUE)
#define HSE_VALUE (8000000UL)
#endif
#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT (100UL)
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE (16000000UL)
#endif
#if !defined(HSI48_VALUE)
#define HSI48_VALUE 48000000U
#endif
#if !defined(LSI_VALUE)
#define LSI_VALUE (32000UL)
#endif
#if !defined(LSE_VALUE)
#define LSE_VALUE (32768UL)
#endif
#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT (5000UL)
#endif
#if !defined(EXTERNAL_I2S1_CLOCK_VALUE)
#define EXTERNAL_I2S1_CLOCK_VALUE (48000UL)
#endif
#if !defined(EXTERNAL_I2S2_CLOCK_VALUE)
#define EXTERNAL_I2S2_CLOCK_VALUE 48000U
#endif

/* ########################### System Configuration ######################### */
#define VDD_VALUE                 (3300UL)
#define TICK_INT_PRIORITY         3U
#define USE_RTOS                  0U
#define PREFETCH_ENABLE           1U
#define INSTRUCTION_CACHE_ENABLE  1U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

/* Includes ------------------------------------------------------------------*/
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32g0xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32g0xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32g0xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32g0xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32g0xx_hal_flash.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32g0xx_hal_iwdg.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32g0xx_hal_pwr.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32g0xx_hal_uart.h"
#endif

/* Exported macro ------------------------------------------------------------*/
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32G0xx_HAL_CONF_H */
