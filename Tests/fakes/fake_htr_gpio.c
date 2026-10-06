#include "fake_htr_gpio.h"
#include <string.h>

static void log_ev(fake_gpio_t *g, uint8_t k, uint8_t a, uint8_t b)
{
    if (g->n < FAKE_GPIO_MAX_EV) { g->ev[g->n].kind = k; g->ev[g->n].a = a; g->ev[g->n].b = b; g->n++; }
}
static void fg_set_line(void *ctx, htr_line_t l, bool active)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    g->line[l] = active;
    log_ev(g, EV_LINE, (uint8_t)l, active);
}
static void fg_attach(void *ctx, uint8_t wiring, bool attach)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    g->attached[wiring] = attach;
    log_ev(g, EV_ATTACH, wiring, attach);
}
/* Deterministic duty: sample k reads high iff (k*pct) crosses a 100 boundary. */
static bool fg_rx(void *ctx, uint8_t wiring)
{
    fake_gpio_t *g = (fake_gpio_t *)ctx;
    uint32_t k = g->reads[wiring]++;
    return ((k * g->high_pct[wiring]) % 100u) + g->high_pct[wiring] >= 100u;
}
void fake_gpio_init(fake_gpio_t *g)
{
    memset(g, 0, sizeof(*g));
    g->api.set_line = fg_set_line;
    g->api.attach_uart = fg_attach;
    g->api.rx_level = fg_rx;
    g->api.ctx = g;
}
void fake_gpio_clear_log(fake_gpio_t *g) { g->n = 0u; }
