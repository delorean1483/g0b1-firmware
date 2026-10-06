#include "fake_htr_driver.h"
#include <string.h>

fake_drv_ctx_t fake_ctx[3];

static void touch(fake_drv_ctx_t *c, uint32_t now) { if (c->alive) { c->last_rx = now; c->any_rx = true; } }
static void f_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx;
    (void)now; c->uart = u; c->inits++; c->probe_calls = 0u; c->any_rx = false;
}
static void f_process(void *ctx, uint32_t now) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->processes++; touch(c, now); }
static void f_start(void *ctx, const htr_request_t *rq) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->starts++; c->last_rq = *rq; }
static void f_stop(void *ctx) { ((fake_drv_ctx_t *)ctx)->stops++; }
static void f_apply(void *ctx, const htr_request_t *rq) { fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx; c->applies++; c->last_rq = *rq; }
static htr_probe_t f_probe(void *ctx, uint32_t now)
{
    fake_drv_ctx_t *c = (fake_drv_ctx_t *)ctx;
    c->probe_calls++;
    if (c->probe_calls < c->probe_after) return HTR_PROBE_BUSY;
    if (c->probe_answer == HTR_PROBE_FOUND) touch(c, now);
    return c->probe_answer;
}
static void f_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const fake_drv_ctx_t *c = (const fake_drv_ctx_t *)ctx;
    (void)now; memset(out, 0, sizeof(*out));
    out->any_rx = c->any_rx; out->last_rx_tick = c->last_rx; out->hint = c->hint;
}
#define FAKE(i, type, wiring) \
    { type, wiring, (uint16_t)(1u << i), &fake_ctx[i], f_init, f_process, f_start, f_stop, f_apply, f_probe, f_status }
const htr_driver_t fake_drv[3] = { FAKE(0, 1u, HTR_WIRING_3W), FAKE(1, 2u, HTR_WIRING_4W), FAKE(2, 3u, HTR_WIRING_3W) };

void fake_drivers_reset(void)
{
    unsigned i;
    memset(fake_ctx, 0, sizeof(fake_ctx));
    for (i = 0u; i < 3u; ++i) { fake_ctx[i].probe_answer = HTR_PROBE_ABSENT; fake_ctx[i].probe_after = 3u; fake_ctx[i].alive = true; fake_ctx[i].hint = HTR_HINT_OFF; }
}
