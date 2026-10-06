#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include <string.h>

typedef enum {
    DET_QUIET = 0,   /* boot TX-quiet                          */
    DET_SENSE,       /* passive line sense, iface off          */
    DET_ENABLE,      /* iface powering for the probe class     */
    DET_PROBE,       /* probing drivers of that class          */
    DET_WAIT,        /* back-off before the next attempt       */
    DET_DONE
} det_t;

static htr_fsm_cfg_t s_cfg;
static uint8_t  s_type_setting;
static uint8_t  s_phase;
static const htr_driver_t *s_drv;      /* active driver (NULL until found) */
static htr_request_t s_rq;
static uint32_t s_phase_tick;
static bool     s_fault_latched;
/* detection */
static det_t    s_det;
static uint32_t s_det_tick;
static uint32_t s_last_sample;
static uint16_t s_samples, s_high_a, s_high_b;
static uint8_t  s_line_sense;
static uint8_t  s_det_result;
static uint8_t  s_probe_wiring;
static uint8_t  s_probe_idx;
static bool     s_probe_inited;
static uint32_t s_stale_since;
static bool     s_heat_seen;      /* heater reported starting/running since this start */
static uint32_t s_restop_tick;    /* rate limit for re-sending stop while FAULT */

static const htr_uart_t *uart_for(uint8_t wiring)
{
    return (wiring == HTR_WIRING_4W) ? s_cfg.uart_4w : s_cfg.uart_3w;
}

static const htr_driver_t *driver_for_type(uint8_t type)
{
    uint8_t i;
    for (i = 0u; i < s_cfg.n_drivers; ++i) {
        if (s_cfg.drivers[i]->type_id == type) return s_cfg.drivers[i];
    }
    return NULL;
}

static void set_phase(uint8_t p, uint32_t now)
{
    s_phase = p;
    s_phase_tick = now;
}

static void begin_sense(uint32_t now)
{
    htr_iface_disable();
    s_drv = NULL;
    s_det = DET_SENSE;
    s_det_tick = now;
    s_last_sample = now - HTR_SENSE_SAMPLE_MS;
    s_samples = s_high_a = s_high_b = 0u;
    s_det_result = HTR_DET_RUNNING;
    s_stale_since = now;
    set_phase(HTR_PHASE_DETECTING, now);
}

static void start_detection(uint32_t now, bool boot)
{
    if (s_type_setting == HTR_TYPE_NONE) {
        htr_iface_disable();
        s_drv = NULL;
        s_det = DET_DONE;
        s_det_result = HTR_DET_IDLE;
        set_phase(HTR_PHASE_OFF, now);
        return;
    }
    if (s_type_setting != HTR_TYPE_AUTO) {
        /* Forced type (bench override): no sensing, no probing. */
        s_drv = driver_for_type(s_type_setting);
        htr_iface_disable();
        if (s_drv == NULL) {
            s_det = DET_DONE;
            s_det_result = HTR_DET_UNKNOWN;
            set_phase(HTR_PHASE_OFF, now);
            return;
        }
        s_probe_wiring = s_drv->wiring;
        s_det = boot ? DET_QUIET : DET_ENABLE;
        s_det_tick = now;
        if (!boot) htr_iface_enable(s_probe_wiring, now);
        set_phase(HTR_PHASE_DETECTING, now);
        return;
    }
    if (boot) {
        htr_iface_disable();
        s_drv = NULL;
        s_det = DET_QUIET;
        s_det_tick = now;
        s_det_result = HTR_DET_RUNNING;
        set_phase(HTR_PHASE_DETECTING, now);
    } else {
        begin_sense(now);
    }
}

/* Next driver index >= from with the probe wiring; n_drivers if none. */
static uint8_t next_probe(uint8_t from)
{
    while ((from < s_cfg.n_drivers) && (s_cfg.drivers[from]->wiring != s_probe_wiring)) from++;
    return from;
}

static void det_fail(uint8_t result, uint32_t now)
{
    htr_iface_disable();
    s_drv = NULL;
    s_det_result = result;
    s_det = DET_WAIT;
    s_det_tick = now;
}

static void found(const htr_driver_t *d, uint32_t now)
{
    s_drv = d;
    s_det = DET_DONE;
    s_det_result = HTR_DET_FOUND;
    s_stale_since = now;
    set_phase(HTR_PHASE_OFF, now);
}

static void detect_step(uint32_t now)
{
    uint8_t cls;
    switch (s_det) {
    case DET_QUIET:
        if ((uint32_t)(now - s_det_tick) < HTR_BOOT_QUIET_MS) return;
        if (s_type_setting == HTR_TYPE_AUTO) {
            begin_sense(now);
        } else {
            s_det = DET_ENABLE;
            s_det_tick = now;
            htr_iface_enable(s_probe_wiring, now);
        }
        return;
    case DET_SENSE:
        if ((uint32_t)(now - s_last_sample) >= HTR_SENSE_SAMPLE_MS) {
            s_last_sample = now;
            s_samples++;
            if (htr_iface_rx_level(HTR_WIRING_3W)) s_high_a++;
            if (htr_iface_rx_level(HTR_WIRING_4W)) s_high_b++;
        }
        if ((uint32_t)(now - s_det_tick) < HTR_SENSE_MS) return;
        {
            uint32_t pa = (100u * s_high_a) / s_samples;
            uint32_t pb = (100u * s_high_b) / s_samples;
            bool a_hi = pa >= HTR_SENSE_HIGH_PCT, b_hi = pb >= HTR_SENSE_HIGH_PCT;
            bool a_lo = pa <= HTR_SENSE_LOW_PCT,  b_lo = pb <= HTR_SENSE_LOW_PCT;
            if (b_hi) cls = HTR_LS_CLASS_4W;
            else if (a_hi && b_lo) cls = HTR_LS_CLASS_3W;
            else if (a_lo && b_lo) cls = HTR_LS_CLASS_NONE;
            else cls = HTR_LS_CLASS_AMBIG;
            s_line_sense = (uint8_t)((a_hi ? HTR_LS_A_IDLE_HIGH : 0u) |
                                     (b_hi ? HTR_LS_B_IDLE_HIGH : 0u) |
                                     (cls << HTR_LS_CLASS_SHIFT));
        }
        if (cls == HTR_LS_CLASS_NONE) { det_fail(HTR_DET_NO_HEATER, now); return; }
        /* Safety: the 4-wire push-pull driver only with the 4-wire signature;
           ambiguous lines get only the open-drain 3-wire probes. */
        s_probe_wiring = (cls == HTR_LS_CLASS_4W) ? HTR_WIRING_4W : HTR_WIRING_3W;
        s_probe_idx = next_probe(0u);
        if (s_probe_idx >= s_cfg.n_drivers) { det_fail(HTR_DET_UNKNOWN, now); return; }
        htr_iface_enable(s_probe_wiring, now);
        s_det = DET_ENABLE;
        s_det_tick = now;
        return;
    case DET_ENABLE:
        if (!htr_iface_ready()) return;
        if (s_type_setting != HTR_TYPE_AUTO) {
            s_drv->init(s_drv->ctx, uart_for(s_drv->wiring), now);
            found(s_drv, now);
            return;
        }
        s_det = DET_PROBE;
        s_probe_inited = false;
        return;
    case DET_PROBE: {
        const htr_driver_t *d = s_cfg.drivers[s_probe_idx];
        htr_probe_t r;
        if (!s_probe_inited) {
            d->init(d->ctx, uart_for(d->wiring), now);
            s_probe_inited = true;
        }
        r = d->probe(d->ctx, now);
        if (r == HTR_PROBE_FOUND) { found(d, now); return; }
        if (r == HTR_PROBE_ABSENT) {
            s_probe_idx = next_probe((uint8_t)(s_probe_idx + 1u));
            s_probe_inited = false;
            if (s_probe_idx >= s_cfg.n_drivers) det_fail(HTR_DET_UNKNOWN, now);
        }
        return;
    }
    case DET_WAIT:
        if ((uint32_t)(now - s_det_tick) >= HTR_DETECT_RETRY_MS) begin_sense(now);
        return;
    default:
        return;
    }
}

static bool fresh_now(const htr_status_t *st, uint32_t now)
{
    return st->any_rx && ((uint32_t)(now - st->last_rx_tick) <= HTR_FRESH_MS);
}

static void enter_fault(uint32_t now)
{
    s_fault_latched = true;
    s_rq.run = 0u;
    s_drv->stop(s_drv->ctx);
    set_phase(HTR_PHASE_FAULT, now);
}

static void run_step(uint32_t now)
{
    htr_status_t st;
    bool fresh;
    uint8_t h;
    s_drv->process(s_drv->ctx, now);
    s_drv->status(s_drv->ctx, now, &st);
    fresh = fresh_now(&st, now);
    h = fresh ? st.hint : (uint8_t)HTR_HINT_UNKNOWN;
    if (fresh) s_stale_since = now;

    switch (s_phase) {
    case HTR_PHASE_OFF:
        if (s_rq.run) {
            s_drv->start(s_drv->ctx, &s_rq);
            s_heat_seen = false;
            set_phase(HTR_PHASE_STARTING, now);
        } else if ((h == HTR_HINT_STARTING) || (h == HTR_HINT_RUNNING)) {
            /* Heater burning with nobody asking for heat (coprocessor reset
               mid-run, or a start processed after a no-op stop): stop it. */
            s_drv->stop(s_drv->ctx);
            set_phase(HTR_PHASE_STOP_REQUESTED, now);
        } else if ((s_type_setting == HTR_TYPE_AUTO) &&
                   ((uint32_t)(now - s_stale_since) >= HTR_HOTSWAP_MS)) {
            begin_sense(now);                     /* hot-swap: only while OFF */
        }
        return;
    case HTR_PHASE_STARTING:
        if (!s_rq.run) { s_drv->stop(s_drv->ctx); set_phase(HTR_PHASE_STOP_REQUESTED, now); return; }
        if (h == HTR_HINT_FAULT) { enter_fault(now); return; }
        if (h == HTR_HINT_RUNNING) { set_phase(HTR_PHASE_RUNNING, now); return; }
        if (h == HTR_HINT_STARTING) s_heat_seen = true;
        /* Failed start: the heater fell back to cooldown, or to standby after
           it had begun starting. (Standby before it acts on the start is normal.) */
        if ((h == HTR_HINT_COOLDOWN) || ((h == HTR_HINT_OFF) && s_heat_seen)) { enter_fault(now); return; }
        if ((uint32_t)(now - s_phase_tick) >= HTR_START_TIMEOUT_MS) enter_fault(now);
        return;
    case HTR_PHASE_RUNNING:
        if (!s_rq.run) { s_drv->stop(s_drv->ctx); set_phase(HTR_PHASE_STOP_REQUESTED, now); return; }
        /* Fault code, or the heater stopping on its own while we want heat. */
        if ((h == HTR_HINT_FAULT) || (h == HTR_HINT_COOLDOWN) || (h == HTR_HINT_OFF)) enter_fault(now);
        return;
    case HTR_PHASE_STOP_REQUESTED:
        if (h == HTR_HINT_COOLDOWN) { set_phase(HTR_PHASE_COOLDOWN, now); return; }
        /* Only a standby report clearly newer than the stop counts: one taken
           just before (or as) the stop went out may predate it. */
        if ((h == HTR_HINT_OFF) &&
            ((int32_t)(st.last_rx_tick - s_phase_tick) >= (int32_t)HTR_STOP_SETTLE_MS)) {
            set_phase(HTR_PHASE_OFF, now);
            return;
        }
        if ((uint32_t)(now - s_phase_tick) >= HTR_COOLDOWN_TIMEOUT_MS) enter_fault(now);
        return;
    case HTR_PHASE_COOLDOWN:
        if (h == HTR_HINT_OFF) { set_phase(HTR_PHASE_OFF, now); return; }
        if ((uint32_t)(now - s_phase_tick) >= HTR_COOLDOWN_TIMEOUT_MS) enter_fault(now);
        return;
    default:  /* FAULT: keep talking, wait for CLEAR_FAULT; re-stop a heater still burning */
        if (((h == HTR_HINT_STARTING) || (h == HTR_HINT_RUNNING)) &&
            ((uint32_t)(now - s_restop_tick) >= 1000u)) {
            s_restop_tick = now;
            s_drv->stop(s_drv->ctx);
        }
        return;
    }
}

void htr_fsm_init(const htr_fsm_cfg_t *cfg, uint8_t type_setting, uint32_t now)
{
    s_cfg = *cfg;
    s_type_setting = type_setting;
    memset(&s_rq, 0, sizeof(s_rq));
    s_rq.level = 1u;
    s_rq.setpoint_c = 20;
    s_rq.ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    s_fault_latched = false;
    s_line_sense = 0u;
    s_drv = NULL;
    htr_iface_init(cfg->gpio, cfg->pwr_for_3w);
    start_detection(now, true);
}

void htr_fsm_process(uint32_t now)
{
    htr_iface_process(now);
    if (s_phase == HTR_PHASE_DETECTING) { detect_step(now); return; }
    if (s_drv != NULL) run_step(now);
}

void htr_fsm_set_request(const htr_request_t *rq)
{
    bool live = (s_rq.level != rq->level) || (s_rq.setpoint_c != rq->setpoint_c) ||
                (s_rq.ambient_c_x10 != rq->ambient_c_x10);
    s_rq.level = rq->level;
    s_rq.setpoint_c = rq->setpoint_c;
    s_rq.ambient_c_x10 = rq->ambient_c_x10;
    /* FAULT holds run at 0 until CLEAR_FAULT (no autonomous restart). */
    s_rq.run = (s_phase == HTR_PHASE_FAULT) ? 0u : (rq->run ? 1u : 0u);
    if (live && (s_drv != NULL) && (s_phase != HTR_PHASE_DETECTING)) s_drv->apply(s_drv->ctx, &s_rq);
}

static bool idle_phase(void)
{
    return (s_phase == HTR_PHASE_OFF) || (s_phase == HTR_PHASE_DETECTING) ||
           (s_phase == HTR_PHASE_FAULT);
}

uint8_t htr_fsm_set_type(uint8_t type_setting, uint32_t now)
{
    if ((type_setting != HTR_TYPE_AUTO) && (type_setting != HTR_TYPE_NONE) &&
        (driver_for_type(type_setting) == NULL)) return HTR_RES_NOT_SUPPORTED;
    /* FAULT exits only through CLEAR_FAULT (heater fresh in standby). */
    if (!idle_phase() || s_rq.run || (s_phase == HTR_PHASE_FAULT)) return HTR_RES_BUSY;
    s_type_setting = type_setting;
    s_fault_latched = false;
    start_detection(now, false);
    return HTR_RES_OK;
}

uint8_t htr_fsm_clear_fault(uint32_t now)
{
    htr_status_t st;
    if (s_phase != HTR_PHASE_FAULT) return HTR_RES_OK;
    s_drv->status(s_drv->ctx, now, &st);
    if (!fresh_now(&st, now) || (st.hint != HTR_HINT_OFF)) return HTR_RES_BUSY;
    s_fault_latched = false;
    set_phase(HTR_PHASE_OFF, now);
    return HTR_RES_OK;
}

uint8_t htr_fsm_redetect(uint32_t now)
{
    if (!idle_phase() || s_rq.run || (s_phase == HTR_PHASE_FAULT)) return HTR_RES_BUSY;
    if (s_type_setting == HTR_TYPE_NONE) return HTR_RES_NO_TYPE;
    start_detection(now, false);
    return HTR_RES_OK;
}

uint8_t htr_fsm_reinit_iface(uint32_t now)
{
    return htr_fsm_redetect(now);   /* re-runs the interface bring-up + driver init */
}

void htr_fsm_view(uint32_t now, htr_fsm_view_t *out)
{
    bool fresh;
    memset(out, 0, sizeof(*out));
    out->phase = s_phase;
    out->line_sense = s_line_sense;
    out->detect_result = s_det_result;
    out->rq = s_rq;
    if ((s_drv != NULL) && (s_phase != HTR_PHASE_DETECTING)) {
        out->active_type = s_drv->type_id;
        out->caps = s_drv->caps;
        s_drv->status(s_drv->ctx, now, &out->st);
        fresh = fresh_now(&out->st, now);
        if (out->st.any_rx) out->flags |= HTR_F_PRESENT;
        if (fresh) out->flags |= HTR_F_FRESH;
        if (fresh && (out->st.hint == HTR_HINT_OFF)) out->flags |= HTR_F_SAFE_OFF;
        if (s_rq.run && !fresh) out->flags |= HTR_F_COMMS_FAULT;
        out->flags |= HTR_F_DETECT_DONE;
    }
    if (htr_iface_ready()) out->flags |= HTR_F_IFACE_POWERED;
    if (s_fault_latched) out->flags |= HTR_F_FAULT_LATCHED;
}
