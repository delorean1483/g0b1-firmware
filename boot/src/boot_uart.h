#ifndef BOOT_UART_H
#define BOOT_UART_H
#include <stdint.h>

/* Bootloader RS-485 Modbus RTU transport (USART1 PA9/PA10, DE=PB3), 9600 8N1,
 * blocking (no DMA, no interrupts). */

void boot_uart_init(void);

/* Assemble one RTU frame into buf (up to max bytes). Bytes are accumulated
 * until a ~4 ms inter-character idle gap (3.5 char times at 9600) is seen after
 * at least 4 bytes; returns the frame length. Returns 0 if no complete frame
 * arrives within timeout_ms of idle. Refreshes the IWDG on each poll. */
uint16_t boot_uart_recv_frame(uint8_t *buf, uint16_t max, uint32_t timeout_ms);

/* Drive DE (PB3) high, transmit len bytes (blocking, waits for TC), drop DE. */
void boot_uart_send(const uint8_t *buf, uint16_t len);

#endif
