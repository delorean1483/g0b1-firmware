#ifndef PORT_H
#define PORT_H
/* EF-L011 port layer: everything MCU-specific behind small functions. */
#include "htr_iface.h"
#include "htr_nv.h"
#include "htr_uart.h"
#include <stdint.h>

void     port_clock_init(void);            /* HSI16 SYSCLK, APB1 /2 = 8 MHz */
void     port_tick_init(void);             /* SysTick 1 ms */
uint32_t port_now(void);
void     port_gpio_init(void);             /* safe levels BEFORE outputs enabled */
extern const htr_gpio_t port_gpio;

void     port_uart_init(void);
void     port_uart_process(uint32_t now);  /* echo-timeout housekeeping */
uint32_t port_uart_echo_errs(void);
extern const htr_uart_t port_uart_3w;
extern const htr_uart_t port_uart_4w;

void     port_i2c_slave_init(uint8_t addr7);
extern const htr_nv_t port_nv;

void     port_iwdg_start(void);
void     port_iwdg_kick(void);
void     port_lock(void);
void     port_unlock(void);
#endif
