/* AUTOTERM 4-wire driver: wake/baud sequencer + online poll scheduler.
   Only the verified request set is ever sent (spec 5.2). */
#include "drv_autoterm.h"
#include "autoterm_proto.h"
#include <string.h>

typedef enum {
    AT_S_WAKE = 0,     /* 2400: wake frames            */
    AT_S_VER4800,      /* 4800: version requests        */
    AT_S_VER9600,      /* 9600: version until a reply   */
    AT_S_ONLINE,       /* 9600: polling + control       */
    AT_S_ABSENT        /* wake attempts exhausted       */
} at_link_t;

typedef struct {
    const htr_uart_t *u;
    at_parser_t parser;
    at_link_t link;
    uint8_t   sent;            /* frames sent in the current link step */
    uint8_t   attempts;        /* wake attempts */
    uint32_t  step_tick;       /* last TX / step start */
    uint32_t  last_tx;
    uint32_t  last_status_tx, last_settings_tx, last_temp_tx;
    uint32_t  last_rx_tick;
    bool      any_rx;
    /* control */
    bool      want_run;        /* start requested and not yet stopped */
    bool      start_pending;
    uint8_t   start_tries;
    bool      stop_pending;
    bool      settings_dirty;
    uint32_t  last_cmd_tx;
    bool      cmd_due;         /* send start/stop at the next TX slot */
    bool      stop_sent;       /* a stop frame has gone out since stop() */
    uint32_t  stop_first_tx;   /* when the first such stop went out */
    int8_t    setpoint_c;
    int16_t   ambient_c_x10;
    /* last decoded status */
    bool      have_status;
    at_status_t st;
    uint32_t  valid_frames;
    uint32_t  transport_errs;
} at_ctx_t;

static at_ctx_t s_ctx;

static void tx(at_ctx_t *c, const uint8_t *f, size_t n, uint32_t now)
{
    if (c->u->write(c->u->ctx, f, n) != n) c->transport_errs++;
    c->last_tx = now;
}

static void tx_simple(at_ctx_t *c, uint8_t cmd, uint32_t now)
{
    uint8_t f[AT_OVERHEAD];
    tx(c, f, AT_BuildSimple(cmd, f), now);
}

static void enter(at_ctx_t *c, at_link_t link, uint32_t baud, uint32_t now)
{
    if ((baud != 0u) && !c->u->set_baud(c->u->ctx, baud)) return; /* TX busy: retry next pass */
    c->link = link;
    c->sent = 0u;
    c->step_tick = now;
    if (baud != 0u) {
        AT_ParserReset(&c->parser);
        c->u->flush_rx(c->u->ctx);
    }
}

static void a_init(void *ctx, const htr_uart_t *u, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    memset(c, 0, sizeof(*c));
    c->u = u;
    c->ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    c->setpoint_c = 20;
    c->link = AT_S_WAKE;
    c->step_tick = now - AT_WAKE_GAP_MS;
    c->last_tx = now - AT_WAKE_GAP_MS;
    (void)c->u->set_baud(c->u->ctx, 2400u);
}

static void rx(at_ctx_t *c, uint32_t now)
{
    uint8_t buf[32];
    at_frame_t fr;
    size_t n, i;
    while ((n = c->u->read(c->u->ctx, buf, sizeof(buf))) > 0u) {
        for (i = 0u; i < n; ++i) {
            if (!AT_ParseByte(&c->parser, buf[i], &fr)) continue;
            c->valid_frames++;
            c->any_rx = true;
            c->last_rx_tick = now;
            if ((c->link == AT_S_VER9600) && (fr.cmd == AT_CMD_VERSION)) {
                c->link = AT_S_ONLINE;
                c->attempts = 0u;
                c->last_status_tx = now - AT_STATUS_EVERY_MS;
                c->last_settings_tx = now;
                c->last_temp_tx = now - AT_TEMP_EVERY_MS;
            }
            if (AT_DecodeStatus(&fr, &c->st)) {
                c->have_status = true;
                /* Start acknowledged once the heater leaves standby; stop once it
                   leaves starting/running (cooldown or standby). */
                if (c->start_pending && (c->st.major != 0u)) c->start_pending = false;
                /* A stop is acked only by a status at least one retry interval
                   after the first stop went out: an earlier standby report may
                   predate the stop (or a start the heater has yet to act on). */
                if (c->stop_pending && c->stop_sent &&
                    ((uint32_t)(now - c->stop_first_tx) >= AT_CMD_RETRY_MS) &&
                    ((c->st.major == 0u) || (c->st.major == 4u))) {
                    c->stop_pending = false;
                }
            }
        }
    }
}

static void link_step(at_ctx_t *c, uint32_t now)
{
    switch (c->link) {
    case AT_S_WAKE:
        if ((uint32_t)(now - c->last_tx) < AT_WAKE_GAP_MS) return;
        if (c->sent >= AT_WAKE_COUNT) { enter(c, AT_S_VER4800, 4800u, now); return; }
        tx_simple(c, AT_CMD_WAKE, now);
        c->sent++;
        return;
    case AT_S_VER4800:
        if ((uint32_t)(now - c->last_tx) < AT_VER_GAP_MS) return;
        if (c->sent >= AT_VER4800_COUNT) { enter(c, AT_S_VER9600, 9600u, now); return; }
        tx_simple(c, AT_CMD_VERSION, now);
        c->sent++;
        return;
    case AT_S_VER9600:
        if ((uint32_t)(now - c->step_tick) >= AT_VER9600_TIMEOUT_MS) {
            c->attempts++;
            if (c->attempts >= AT_WAKE_ATTEMPTS) { c->link = AT_S_ABSENT; return; }
            enter(c, AT_S_WAKE, 2400u, now);
            return;
        }
        if ((uint32_t)(now - c->last_tx) < AT_VER_GAP_MS) return;
        tx_simple(c, AT_CMD_VERSION, now);
        return;
    default:
        return;
    }
}

static void online_step(at_ctx_t *c, uint32_t now)
{
    uint8_t f[AT_MAX_FRAME];
    if ((uint32_t)(now - c->last_rx_tick) >= AT_LINK_LOST_MS) {
        enter(c, AT_S_WAKE, 2400u, now);      /* re-wake; harmless to a running heater */
        c->attempts = 0u;
        return;
    }
    if ((uint32_t)(now - c->last_tx) < AT_TX_SLOT_MS) return;

    if (c->stop_pending &&
        (c->cmd_due || ((uint32_t)(now - c->last_cmd_tx) >= AT_CMD_RETRY_MS))) {
        tx_simple(c, AT_CMD_STOP, now);
        if (!c->stop_sent) { c->stop_sent = true; c->stop_first_tx = now; }
        c->last_cmd_tx = now;
        c->cmd_due = false;
        return;
    }
    if (c->start_pending &&
        (c->cmd_due || ((uint32_t)(now - c->last_cmd_tx) >= AT_CMD_RETRY_MS))) {
        if (c->start_tries >= AT_START_TRIES) {
            c->start_pending = false;          /* FSM start timeout will raise FAULT */
        } else {
            tx(c, f, AT_BuildStart(c->setpoint_c, f), now);
            c->start_tries++;
            c->last_cmd_tx = now;
            c->cmd_due = false;
            return;
        }
    }
    if (c->settings_dirty && c->want_run && !c->start_pending) {
        tx(c, f, AT_BuildSettingsWrite(c->setpoint_c, f), now);
        c->settings_dirty = false;
        return;
    }
    if ((uint32_t)(now - c->last_status_tx) >= AT_STATUS_EVERY_MS) {
        tx_simple(c, AT_CMD_STATUS, now);
        c->last_status_tx = now;
        return;
    }
    if ((uint32_t)(now - c->last_settings_tx) >= AT_SETTINGS_EVERY_MS) {
        tx_simple(c, AT_CMD_SETTINGS, now);
        c->last_settings_tx = now;
        return;
    }
    if ((c->ambient_c_x10 != HTR_AMBIENT_UNKNOWN) &&
        ((uint32_t)(now - c->last_temp_tx) >= AT_TEMP_EVERY_MS)) {
        int16_t a = c->ambient_c_x10;
        int8_t whole = (int8_t)((a >= 0) ? ((a + 5) / 10) : ((a - 5) / 10));
        tx(c, f, AT_BuildTemp(whole, f), now);
        c->last_temp_tx = now;
    }
}

static void a_process(void *ctx, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    rx(c, now);
    if (c->link == AT_S_ONLINE) online_step(c, now);
    else link_step(c, now);
}

static void a_apply(void *ctx, const htr_request_t *rq)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    if (rq->setpoint_c != c->setpoint_c) {
        c->setpoint_c = rq->setpoint_c;
        c->settings_dirty = true;
    }
    c->ambient_c_x10 = rq->ambient_c_x10;
}

static void a_start(void *ctx, const htr_request_t *rq)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    a_apply(ctx, rq);
    c->settings_dirty = false;       /* setpoint travels in the start frame */
    c->want_run = true;
    c->stop_pending = false;
    c->start_pending = true;
    c->start_tries = 0u;
    c->cmd_due = true;
}

static void a_stop(void *ctx)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    c->want_run = false;
    c->start_pending = false;
    c->stop_pending = true;
    c->stop_sent = false;
    c->cmd_due = true;
}

/* Probe = the link sequence only: wake + version requests. */
static htr_probe_t a_probe(void *ctx, uint32_t now)
{
    at_ctx_t *c = (at_ctx_t *)ctx;
    rx(c, now);
    if (c->link == AT_S_ONLINE) return HTR_PROBE_FOUND;
    if (c->link == AT_S_ABSENT) return HTR_PROBE_ABSENT;
    link_step(c, now);
    return HTR_PROBE_BUSY;
}

static void a_status(const void *ctx, uint32_t now, htr_status_t *out)
{
    const at_ctx_t *c = (const at_ctx_t *)ctx;
    (void)now;
    memset(out, 0, sizeof(*out));
    out->valid_frames = c->valid_frames;
    out->crc_fails = c->parser.crc_fails;
    out->transport_errs = c->transport_errs;
    out->any_rx = c->any_rx;
    out->last_rx_tick = c->last_rx_tick;
    if (!c->have_status) return;
    out->hint = AT_PhaseHint(c->st.major, c->st.minor);
    out->vendor_state = c->st.major;
    out->vendor_sub = c->st.minor;
    out->supply_mv = c->st.supply_mv;
    out->exch_raw = c->st.exch_raw;
    /* Provisional fields are carried but NOT marked valid (spec 5.2). */
    out->fan_rpm = (uint16_t)((uint16_t)c->st.fan_rps * 60u);
    out->pump_hz_x10 = (uint16_t)(c->st.pump_hz_x100 / 10u);
    out->valid = HTR_V_SUPPLY_MV | HTR_V_EXCH_RAW;
}

const htr_driver_t drv_autoterm = {
    HTR_TYPE_AUTOTERM, HTR_WIRING_4W,
    HTR_CAP_SETPOINT | HTR_CAP_AMBIENT_IN | HTR_CAP_SUPPLY_V | HTR_CAP_EXCH_TEMP,
    &s_ctx,
    a_init, a_process, a_start, a_stop, a_apply, a_probe, a_status
};
