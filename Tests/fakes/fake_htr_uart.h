#ifndef FAKE_HTR_UART_H
#define FAKE_HTR_UART_H
/* In-memory htr_uart_t: records everything written (with the baud it was
   written at), serves injected RX bytes, and can refuse writes/baud changes. */
#include "htr_uart.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAKE_UART_TX_CAP 4096u
#define FAKE_UART_RX_CAP 4096u
#define FAKE_UART_MAX_WRITES 512u

typedef struct {
    htr_uart_t api;
    uint8_t  tx[FAKE_UART_TX_CAP];
    size_t   tx_len;
    size_t   write_off[FAKE_UART_MAX_WRITES];  /* start offset of each write() */
    size_t   write_len[FAKE_UART_MAX_WRITES];
    uint32_t write_baud[FAKE_UART_MAX_WRITES];
    size_t   writes;
    uint8_t  rx[FAKE_UART_RX_CAP];
    size_t   rx_head, rx_tail;
    uint32_t baud;
    bool     refuse_write;
    bool     refuse_baud;
    unsigned flushes;
} fake_uart_t;

void fake_uart_init(fake_uart_t *f);
void fake_uart_inject(fake_uart_t *f, const uint8_t *buf, size_t len);
void fake_uart_clear_tx(fake_uart_t *f);
/* Command byte (offset 4) of write #i, for AUTOTERM frames. */
uint8_t fake_uart_write_cmd(const fake_uart_t *f, size_t i);
#endif
