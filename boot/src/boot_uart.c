/* boot_uart.c — bootloader RS-485 Modbus RTU transport (blocking).
 *
 * USART1 PA9(TX)/PA10(RX) 9600 8N1, DE on PB3 driven as a plain push-pull GPIO
 * output (NOT the USART hardware DE — the boot path stays simple and blocking;
 * no DMA, no interrupts). Frames are delimited by a ~4 ms (3.5 char at 9600)
 * inter-character idle gap, per Modbus RTU.
 */
#include "stm32g0xx_hal.h"
#include "boot_uart.h"

void Error_Handler(void);            /* defined in boot_main.c */
extern IWDG_HandleTypeDef hiwdg;     /* defined in boot_clock.c */

#define DE_PORT   GPIOB
#define DE_PIN    GPIO_PIN_3

/* 3.5 char times at 9600 8N1 (~1.04 ms/char) ~= 3.6 ms -> 4 ms gap. */
#define RTU_GAP_MS   4u

static UART_HandleTypeDef huart;

void boot_uart_init(void)
{
  GPIO_InitTypeDef g = {0};
  RCC_PeriphCLKInitTypeDef pclk = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USART1 kernel clock = PCLK1 (matches the app's BRR at 9600). */
  pclk.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  pclk.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_USART1_CLK_ENABLE();

  /* PA9 TX / PA10 RX (AF1). */
  g.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = GPIO_AF1_USART1;
  HAL_GPIO_Init(GPIOA, &g);

  /* PB3 = DE, push-pull output, start low (receive mode). */
  HAL_GPIO_WritePin(DE_PORT, DE_PIN, GPIO_PIN_RESET);
  g.Pin = DE_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = 0;
  HAL_GPIO_Init(DE_PORT, &g);

  huart.Instance = USART1;
  huart.Init.BaudRate = 9600;
  huart.Init.WordLength = UART_WORDLENGTH_8B;
  huart.Init.StopBits = UART_STOPBITS_1;
  huart.Init.Parity = UART_PARITY_NONE;
  huart.Init.Mode = UART_MODE_TX_RX;
  huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart.Init.OverSampling = UART_OVERSAMPLING_16;
  huart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart) != HAL_OK)
  {
    Error_Handler();
  }
  (void)HAL_UARTEx_SetTxFifoThreshold(&huart, UART_TXFIFO_THRESHOLD_1_8);
  (void)HAL_UARTEx_SetRxFifoThreshold(&huart, UART_RXFIFO_THRESHOLD_1_8);
  (void)HAL_UARTEx_DisableFifoMode(&huart);
}

static void clear_rx_errors(void)
{
  __HAL_UART_CLEAR_FLAG(&huart, UART_CLEAR_OREF | UART_CLEAR_FEF
                              | UART_CLEAR_NEF | UART_CLEAR_PEF);
}

uint16_t boot_uart_recv_frame(uint8_t *buf, uint16_t max, uint32_t timeout_ms)
{
  uint16_t idx = 0;
  uint32_t start = HAL_GetTick();

  for (;;)
  {
    uint8_t b;
    HAL_IWDG_Refresh(&hiwdg);

    /* Absolute cap on the whole call, checked UNCONDITIONALLY every iteration.
       On a continuously noisy/contended bus HAL_UART_Receive can keep returning
       HAL_OK, so a budget check confined to the idle/error branch would never
       be reached and this loop would refresh the IWDG forever (defeating the
       watchdog). Bounding here guarantees the call returns within timeout_ms; a
       legitimate frame (~260 ms max at 9600) still completes via the idle-gap
       path well before the caller's 1 s budget. */
    if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
    {
      return 0;
    }

    HAL_StatusTypeDef st = HAL_UART_Receive(&huart, &b, 1u, RTU_GAP_MS);
    if (st == HAL_OK)
    {
      if (idx < max)
      {
        buf[idx++] = b;
      }
      /* still receiving — keep going */
    }
    else
    {
      /* HAL_TIMEOUT (inter-frame idle gap) or a line error. */
      clear_rx_errors();               /* keep ORE from wedging reception */
      if (idx >= 4u)
      {
        return idx;                    /* a complete RTU frame */
      }
      idx = 0;                         /* discard a runt / line noise */
    }
  }
}

void boot_uart_send(const uint8_t *buf, uint16_t len)
{
  HAL_GPIO_WritePin(DE_PORT, DE_PIN, GPIO_PIN_SET);          /* DE high: drive bus */

  /* HAL_UART_Transmit polls TXE per byte and then waits for TC on the last
     byte, all bounded by the 1000 ms timeout budget — so it is already the
     bounded TC wait. On HAL_OK the last stop bit has left the shifter; on any
     non-OK result (a real UART/RS-485 fault) we must NOT spin re-checking TC
     (that would feed the IWDG forever with DE stuck high, jamming the bus). We
     drop DE and return on EVERY path either way. */
  HAL_StatusTypeDef st = HAL_UART_Transmit(&huart, (uint8_t *)buf, len, 1000u);
  (void)st;

  HAL_GPIO_WritePin(DE_PORT, DE_PIN, GPIO_PIN_RESET);        /* DE low on every exit path */
}
