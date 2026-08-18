#ifndef DRV_MODBUS_UART_H
#define DRV_MODBUS_UART_H

/* RS-485 Modbus RTU slave transport over USART1 + DMA + hardware Driver-Enable
 * (Task 5). Bridges the physical display bus to the portable mb_engine register
 * model: it assembles received RTU frames, hands them to mb_engine_process(),
 * and DMA-transmits the response (the USART's hardware DE frames the TX).
 *
 * Framing: variable-length RX via HAL_UARTEx_ReceiveToIdle_DMA — the USART
 * IDLE-line event marks the RTU inter-frame gap (functionally the 3.5-char gap;
 * see the note in drv_modbus_uart.c on IDLE vs. the spec's RTO). Bus params:
 * 9600 8N1, slave address 1 ("EF-G0B1R"), matching the PIC display link.
 *
 * Bench bring-up plan Task 5:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */

/* Start RS-485 reception. Call once after mb_engine_init() + provider registration
 * and after MX_USART1_UART_Init()/MX_DMA_Init() (generated main() runs those). */
void drv_modbus_uart_init(void);

/* Superloop step: if a full RTU frame was assembled, run it through
 * mb_engine_process() and DMA-transmit any response. Never call from an ISR. */
void drv_modbus_uart_poll(void);

#endif /* DRV_MODBUS_UART_H */
