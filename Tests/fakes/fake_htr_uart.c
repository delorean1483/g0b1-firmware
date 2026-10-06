#include "fake_htr_uart.h"
#include <string.h>

static size_t fu_write(void *ctx, const uint8_t *buf, size_t len)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    if (f->refuse_write || (f->tx_len + len > FAKE_UART_TX_CAP) ||
        (f->writes >= FAKE_UART_MAX_WRITES)) {
        return 0u;
    }
    f->write_off[f->writes] = f->tx_len;
    f->write_len[f->writes] = len;
    f->write_baud[f->writes] = f->baud;
    f->writes++;
    memcpy(&f->tx[f->tx_len], buf, len);
    f->tx_len += len;
    return len;
}

static size_t fu_read(void *ctx, uint8_t *buf, size_t cap)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    size_t n = 0u;
    while ((n < cap) && (f->rx_tail != f->rx_head)) {
        buf[n++] = f->rx[f->rx_tail];
        f->rx_tail = (f->rx_tail + 1u) % FAKE_UART_RX_CAP;
    }
    return n;
}

static void fu_flush(void *ctx)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    f->rx_tail = f->rx_head;
    f->flushes++;
}

static bool fu_set_baud(void *ctx, uint32_t baud)
{
    fake_uart_t *f = (fake_uart_t *)ctx;
    if (f->refuse_baud) return false;
    f->baud = baud;
    return true;
}

void fake_uart_init(fake_uart_t *f)
{
    memset(f, 0, sizeof(*f));
    f->api.write = fu_write;
    f->api.read = fu_read;
    f->api.flush_rx = fu_flush;
    f->api.set_baud = fu_set_baud;
    f->api.ctx = f;
}

void fake_uart_inject(fake_uart_t *f, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0u; i < len; ++i) {
        f->rx[f->rx_head] = buf[i];
        f->rx_head = (f->rx_head + 1u) % FAKE_UART_RX_CAP;
    }
}

void fake_uart_clear_tx(fake_uart_t *f)
{
    f->tx_len = 0u;
    f->writes = 0u;
}

uint8_t fake_uart_write_cmd(const fake_uart_t *f, size_t i)
{
    return (i < f->writes && f->write_len[i] > 4u) ? f->tx[f->write_off[i] + 4u] : 0xFFu;
}
