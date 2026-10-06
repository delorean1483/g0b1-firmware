#ifndef HTR_UART_H
#define HTR_UART_H
/* Byte-stream seam between heater protocol code and a UART.
   The port implements it with IRQ-driven rings; tests use a fake.
   On half-duplex (3-wire) links the port strips the local TX echo, so
   read() only ever returns bytes sent by the heater. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct htr_uart {
    size_t (*write)(void *ctx, const uint8_t *buf, size_t len); /* bytes queued */
    size_t (*read)(void *ctx, uint8_t *buf, size_t cap);        /* bytes drained */
    void   (*flush_rx)(void *ctx);
    bool   (*set_baud)(void *ctx, uint32_t baud);               /* false: TX busy, retry */
    void   *ctx;
} htr_uart_t;

#endif /* HTR_UART_H */
