/* boot_clock.c — bootloader system clock + IWDG.
 *
 * boot_clock_init() is SystemClock_Config() copied verbatim from the app
 * (cube/Core/Src/main.c): HSE -> PLL, FLASH_LATENCY_2. Identical SYSCLK means
 * an identical USART1 BRR, so the boot's 9600 8N1 timing matches the cortex the
 * running app already talks to.
 *
 * boot_iwdg_init() gives the bootloader a DELIBERATELY LONG watchdog
 * (PRESCALER_256, Window 4095, Reload 4095 -> ~32 s at LSI/256), NOT the app's
 * tight ~2 s. Rationale: a full 224 KB slot erase is 112 synchronous page
 * erases (~3-4 s), and a single HAL_FLASHEx_Erase can stall the CPU longer
 * than the app's 2 s window on real silicon (bench 2026-09-14: a slot erase
 * tripped the 2 s IWDG and left the unit unbootable). The bootloader is a
 * controlled recovery environment, so a coarse ~32 s watchdog still catches a
 * genuinely hung bootloader while leaving ample headroom for flash ops. The
 * IWDG hardware is shared: whichever of app/bootloader runs HAL_IWDG_Init last
 * wins, and the app re-arms its own 2 s on the JUMP path. drv_iflash.c still
 * refreshes hiwdg before each page erase as a second layer.
 */
#include "stm32g0xx_hal.h"

void Error_Handler(void);   /* defined in boot_main.c */

IWDG_HandleTypeDef hiwdg;

void boot_clock_init(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* PWR is clocked from the APB but its gate must be on before touching the
     voltage-scaling register (the app relies on HAL_MspInit for this; the boot
     has no MspInit, so enable it explicitly here). Idempotent. */
  __HAL_RCC_PWR_CLK_ENABLE();

  /** Configure the main internal regulator output voltage */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure. */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

void boot_iwdg_init(void)
{
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Window = 4095;
  hiwdg.Init.Reload = 4095;   /* ~32 s at LSI/256 -- headroom for slot erase */
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
}
