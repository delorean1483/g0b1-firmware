/* IRQ-driven byte rings for LPUART1 (3-wire) and USART2 (4-wire).
   3-wire is half-duplex on one wire: every byte we transmit comes back on
   RX; it is matched against an echo queue and dropped (mismatch counts as
   an echo error = TX-path diagnostic). */
#include "port.h"
#include "htr_board.h"
#include <stdbool.h>

#define RX_SZ   64u
#define TX_SZ   32u
#define ECHO_SZ 32u
#define ECHO_TIMEOUT_MS 20u
#define PCLK1_HZ 8000000u

typedef struct {
    USART_TypeDef *u;
    bool lp;                   /* LPUART: BRR = 256*f/baud */
    bool half;                 /* strip local echo */
    volatile uint8_t rx[RX_SZ];
    volatile uint8_t rx_h, rx_t;
    volatile uint8_t tx[TX_SZ];
    volatile uint8_t tx_h, tx_t;
    volatile uint8_t echo[ECHO_SZ];
    volatile uint8_t e_h, e_t;
    volatile uint32_t last_tx_ms;
} pu_t;

static pu_t s_3w;   /* zero-init (.bss); identity set in port_uart_init() */
static pu_t s_4w;
static volatile uint32_t s_echo_errs;

static uint32_t brr(const pu_t *p, uint32_t baud)
{
    return p->lp ? (uint32_t)(((uint64_t)PCLK1_HZ * 256u + baud / 2u) / baud)
                 : (PCLK1_HZ + baud / 2u) / baud;
}

static void cfg(pu_t *p, uint32_t baud, bool txinv, bool rxinv)
{
    p->u->CR1 = 0u;
    p->u->CR2 = (txinv ? USART_CR2_TXINV : 0u) | (rxinv ? USART_CR2_RXINV : 0u);
    p->u->BRR = brr(p, baud);
    p->u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
}

static void irq(pu_t *p)
{
    uint32_t isr = p->u->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        p->u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
    }
    if (isr & USART_ISR_RXNE) {
        uint8_t b = (uint8_t)p->u->RDR;
        if (p->half && (p->e_t != p->e_h)) {
            uint8_t exp = p->echo[p->e_t];
            p->e_t = (uint8_t)((p->e_t + 1u) % ECHO_SZ);
            if (b != exp) s_echo_errs++;
        } else {
            uint8_t n = (uint8_t)((p->rx_h + 1u) % RX_SZ);
            if (n != p->rx_t) { p->rx[p->rx_h] = b; p->rx_h = n; }
        }
    }
    if ((p->u->CR1 & USART_CR1_TXEIE) && (isr & USART_ISR_TXE)) {
        if (p->tx_t != p->tx_h) {
            uint8_t b = p->tx[p->tx_t];
            p->tx_t = (uint8_t)((p->tx_t + 1u) % TX_SZ);
            if (p->half) {
                p->echo[p->e_h] = b;
                p->e_h = (uint8_t)((p->e_h + 1u) % ECHO_SZ);
            }
            p->last_tx_ms = port_now();
            p->u->TDR = b;
        } else {
            p->u->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

void LPUART1_IRQHandler(void) { irq(&s_3w); }
void USART2_IRQHandler(void)  { irq(&s_4w); }

static size_t u_write(void *ctx, const uint8_t *buf, size_t len)
{
    pu_t *p = (pu_t *)ctx;
    uint8_t used = (uint8_t)((p->tx_h + TX_SZ - p->tx_t) % TX_SZ);
    size_t i;
    if (len > (size_t)(TX_SZ - 1u - used)) return 0u;      /* all or nothing */
    for (i = 0u; i < len; ++i) {
        p->tx[p->tx_h] = buf[i];
        p->tx_h = (uint8_t)((p->tx_h + 1u) % TX_SZ);
    }
    p->u->CR1 |= USART_CR1_TXEIE;
    return len;
}

static size_t u_read(void *ctx, uint8_t *buf, size_t cap)
{
    pu_t *p = (pu_t *)ctx;
    size_t n = 0u;
    while ((n < cap) && (p->rx_t != p->rx_h)) {
        buf[n++] = p->rx[p->rx_t];
        p->rx_t = (uint8_t)((p->rx_t + 1u) % RX_SZ);
    }
    return n;
}

static void u_flush(void *ctx)
{
    pu_t *p = (pu_t *)ctx;
    p->rx_t = p->rx_h;
}

static bool u_set_baud(void *ctx, uint32_t baud)
{
    pu_t *p = (pu_t *)ctx;
    if ((p->tx_t != p->tx_h) || ((p->u->ISR & USART_ISR_TC) == 0u)) return false;
    p->u->CR1 &= ~USART_CR1_UE;
    p->u->BRR = brr(p, baud);
    p->u->CR1 |= USART_CR1_UE;
    return true;
}

const htr_uart_t port_uart_3w = { u_write, u_read, u_flush, u_set_baud, &s_3w };
const htr_uart_t port_uart_4w = { u_write, u_read, u_flush, u_set_baud, &s_4w };

void port_uart_init(void)
{
    s_3w.u = HB_3W_UART; s_3w.lp = true;  s_3w.half = true;
    s_4w.u = HB_4W_UART; s_4w.lp = false; s_4w.half = false;
    RCC->APB1ENR |= RCC_APB1ENR_LPUART1EN | RCC_APB1ENR_USART2EN;  /* both on PCLK1 */
    cfg(&s_3w, 4800u, HB_3W_TX_INVERT, HB_3W_RX_INVERT);
    cfg(&s_4w, 2400u, HB_4W_TX_INVERT, HB_4W_RX_INVERT);
    NVIC_SetPriority(HB_3W_IRQn, 1u);
    NVIC_SetPriority(HB_4W_IRQn, 1u);
    NVIC_EnableIRQ(HB_3W_IRQn);
    NVIC_EnableIRQ(HB_4W_IRQn);
}

/* An echo that never comes back means the RX path or the wire is broken. */
void port_uart_process(uint32_t now)
{
    port_lock();
    if ((s_3w.e_t != s_3w.e_h) && (s_3w.tx_t == s_3w.tx_h) &&
        ((uint32_t)(now - s_3w.last_tx_ms) > ECHO_TIMEOUT_MS)) {
        s_3w.e_t = s_3w.e_h;
        s_echo_errs++;
    }
    port_unlock();
}

uint32_t port_uart_echo_errs(void) { return s_echo_errs; }
