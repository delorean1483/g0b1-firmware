/* htr_driver_t adapter over the verified VEVOR protocol (vevor_proto.c). */
#include "drv_vevor.h"
#include "vevor_proto.h"
#include <string.h>

#define VEVOR_PROBE_TIMEOUT_MS 3500u   /* ~3 stop polls without a valid reply */

typedef struct {
    VEVOR_t  v;
    uint32_t init_tick;
} vevor_ctx_t;

static vevor_ctx_t s_ctx;

static void v_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    vevor_ctx_t *c = (vevor_ctx_t *)ctx;
    (void)VEVOR_Init(&c->v, u, now);
    c->init_tick = now;
}

static void v_process(void *ctx, uint32_t now)
{
    VEVOR_Process(&((vevor_ctx_t *)ctx)->v, now);
}

static void v_start(void *ctx, const htr_request_t *rq)
{
    (void)VEVOR_Start(&((vevor_ctx_t *)ctx)->v, rq->level);
}

static void v_stop(void *ctx)
{
    VEVOR_Stop(&((vevor_ctx_t *)ctx)->v);
}

static void v_apply(void *ctx, const htr_request_t *rq)
{
    (void)VEVOR_SetLevel(&((vevor_ctx_t *)ctx)->v, rq->level);
}

/* The VEVOR answers only control frames; its standby poll is a STOP frame,
   which is harmless to an idle heater (and starts a normal cooldown on a
   running one -- see spec 4.1). */
static htr_probe_t v_probe(void *ctx, uint32_t now)
{
    vevor_ctx_t *c = (vevor_ctx_t *)ctx;
    VEVOR_Process(&c->v, now);
    if (c->v.valid_frames > 0u) return HTR_PROBE_FOUND;
    if ((uint32_t)(now - c->init_tick) >= VEVOR_PROBE_TIMEOUT_MS) return HTR_PROBE_ABSENT;
    return HTR_PROBE_BUSY;
}

static uint8_t hint_for(uint8_t state, uint8_t error)
{
    if (error != 0u) return HTR_HINT_FAULT;   /* heater-reported error code (bench-confirm codes) */
    switch (state) {
    case VEVOR_REPORTED_OFF:      return HTR_HINT_OFF;
    case VEVOR_REPORTED_PREHEAT:
    case VEVOR_REPORTED_IGNITION: return HTR_HINT_STARTING;
    case VEVOR_REPORTED_RUNNING:  return HTR_HINT_RUNNING;
    case VEVOR_REPORTED_COOLDOWN: return HTR_HINT_COOLDOWN;
    default:                      return HTR_HINT_UNKNOWN;
    }
}

static void v_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const VEVOR_t *v = &((const vevor_ctx_t *)ctx)->v;
    (void)now;
    memset(out, 0, sizeof(*out));
    out->valid_frames = v->valid_frames;
    out->crc_fails = v->checksum_failures;
    out->transport_errs = v->transport_errors;
    if (!v->status.valid) return;
    out->any_rx = true;
    out->last_rx_tick = v->status.received_tick;
    out->hint = hint_for(v->status.state, v->status.error);
    out->vendor_state = v->status.state;
    out->fault_code = v->status.error;
    out->active_level = v->status.level;
    out->supply_mv = v->status.supply_mv;
    out->fan_rpm = v->status.fan_rpm;
    out->pump_hz_x10 = v->status.pump_hz_x10;
    out->exch_raw = v->status.exchanger_raw;
    out->state_seconds = v->status.state_seconds;
    out->valid = HTR_V_FAULT_CODE | HTR_V_ACTIVE_LEVEL | HTR_V_SUPPLY_MV |
                 HTR_V_FAN_RPM | HTR_V_PUMP_HZ | HTR_V_EXCH_RAW |
                 HTR_V_STATE_SECONDS;
}

const htr_driver_t drv_vevor = {
    HTR_TYPE_VEVOR, HTR_WIRING_3W,
    HTR_CAP_LEVEL | HTR_CAP_FAN_RPM | HTR_CAP_PUMP | HTR_CAP_SUPPLY_V |
        HTR_CAP_EXCH_TEMP | HTR_CAP_FAULT_CODE,
    &s_ctx,
    v_init, v_process, v_start, v_stop, v_apply, v_probe, v_status
};
