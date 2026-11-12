#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "htr_version.h"
#include <string.h>

#define ECHO_STICKY_MS 3000u

static htr_regs_cfg_t s_cfg;
/* live: written by the superloop; snap: frozen copy served to one read txn. */
static uint8_t s_live[HTR_REG_COUNT];
static uint8_t s_snap[HTR_REG_COUNT];
/* staged master writes (complete transactions only), consumed by htr_regs_poll() */
static uint8_t s_stage[HTR_REG_COUNT];
static uint8_t s_dirty[HTR_REG_COUNT];
static volatile bool s_commit;
static volatile bool s_stop_seen;   /* a committed stop, kept even if a later write overwrites it */
/* ISR transaction state. A write is buffered until its STOP and dropped if a
   bus error or repeated START ends it first, or if it strays outside the
   control block. */
static bool s_in_read;
static bool s_have_ptr;
static uint8_t s_ptr;
static bool s_wr_open;
static bool s_wr_bad;
static uint8_t s_wr_first;
static uint8_t s_wr_len;
static uint8_t s_wr_buf[HTR_CTRL_END - HTR_CTRL_FIRST];
static volatile uint16_t s_discards;
/* control state owned by the superloop */
static htr_request_t s_rq;
static uint8_t s_type_setting;
static uint8_t s_timeout_s;
static uint8_t s_cmd_result;
static uint32_t s_host_last;      /* last valid (changed) heartbeat */
static bool s_hb_have;
static uint8_t s_hb_last;
static bool s_host_live;
static bool s_host_stop;
static bool s_armed;              /* a clean stop since run last dropped */
static bool s_refused;
static uint32_t s_prev_echo;
static uint32_t s_echo_tick;
static bool s_echo_seen;

static void lock(void)   { if (s_cfg.lock) s_cfg.lock(); }
static void unlock(void) { if (s_cfg.unlock) s_cfg.unlock(); }

static void put16(uint8_t *m, uint8_t r, uint16_t v) { m[r] = (uint8_t)(v & 0xFFu); m[r + 1u] = (uint8_t)(v >> 8); }
static uint16_t sat16(uint32_t v) { return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v; }

static void fill_control(uint8_t *m)
{
    m[HTR_R_HEATER_TYPE] = s_type_setting;
    m[HTR_R_RUN_REQUEST] = s_rq.run ? HTR_RUN_KEY : 0u;
    m[HTR_R_LEVEL] = s_rq.level;
    m[HTR_R_SETPOINT_C] = (uint8_t)s_rq.setpoint_c;
    put16(m, HTR_R_AMBIENT_C_X10, (uint16_t)s_rq.ambient_c_x10);
    m[HTR_R_HOST_TIMEOUT_S] = s_timeout_s;
    m[HTR_R_HOST_HEARTBEAT] = s_hb_last;
    m[HTR_R_COMMAND] = 0u;
    m[HTR_R_CMD_RESULT] = s_cmd_result;
}

uint8_t htr_regs_init(const htr_regs_cfg_t *cfg, uint32_t now)
{
    s_cfg = *cfg;
    memset(s_live, 0, sizeof(s_live));
    memset(s_dirty, 0, sizeof(s_dirty));
    s_commit = false;
    s_stop_seen = false;
    s_in_read = false;
    s_have_ptr = false;
    s_ptr = 0u;
    s_wr_open = false;
    s_discards = 0u;
    s_rq.run = 0u;
    s_rq.level = 1u;
    s_rq.setpoint_c = 20;
    s_rq.ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    s_cmd_result = HTR_RES_OK;
    s_timeout_s = HTR_HOST_TIMEOUT_DEFAULT_S;
    s_host_last = now;
    s_hb_have = false;
    s_hb_last = 0u;
    s_host_live = false;
    s_host_stop = false;
    s_armed = false;
    s_refused = false;
    s_prev_echo = 0u;
    s_echo_seen = false;
    {
        uint8_t legacy_min;   /* map v1 minutes byte: ignored */
        if ((cfg->nv == NULL) || !cfg->nv->load(cfg->nv->ctx, &s_type_setting, &legacy_min)) {
            s_type_setting = HTR_NV_DEFAULT_TYPE;
        }
    }
    s_live[HTR_R_DEVICE_ID] = HTR_DEVICE_ID;
    s_live[HTR_R_MAP_VERSION] = HTR_MAP_VERSION;
    s_live[HTR_R_FW_MAJOR] = HTR_FW_MAJOR;
    s_live[HTR_R_FW_MINOR] = HTR_FW_MINOR;
    put16(s_live, HTR_R_FW_BUILD, HTR_FW_BUILD);
    put16(s_live, HTR_R_SUPPORTED_TYPES, cfg->supported_types);
    s_live[HTR_R_HW_ID] = cfg->hw_id;
    fill_control(s_live);
    return s_type_setting;
}

uint8_t htr_regs_host_timeout_s(void) { return s_timeout_s; }

/* ---------------- ISR side ---------------- */
static void wr_drop(void)
{
    if (s_wr_open && ((s_wr_len != 0u) || s_wr_bad)) s_discards++;
    s_wr_open = false;
}

void htr_regs_i2c_begin(bool is_read)
{
    wr_drop();                                    /* cut by a repeated START: never applied */
    s_in_read = is_read;
    if (is_read) {
        memcpy(s_snap, s_live, sizeof(s_snap));   /* one consistent image per read */
    } else {
        s_have_ptr = false;
        s_wr_open = true;
        s_wr_bad = false;
        s_wr_len = 0u;
    }
}

void htr_regs_i2c_rx(uint8_t byte)
{
    if (!s_have_ptr) { s_ptr = byte; s_wr_first = byte; s_have_ptr = true; return; }
    if (s_wr_open && !s_wr_bad) {
        if ((s_ptr < HTR_CTRL_FIRST) || (s_ptr >= HTR_CTRL_END)) {
            s_wr_bad = true;
        } else {
            s_wr_buf[s_ptr - HTR_CTRL_FIRST] = byte;
            s_wr_len++;
        }
    }
    s_ptr++;
}

uint8_t htr_regs_i2c_tx(void)
{
    uint8_t v = (s_ptr < HTR_REG_COUNT) ? s_snap[s_ptr] : 0xFFu;
    s_ptr++;
    return v;
}

void htr_regs_i2c_end(void)
{
    uint8_t i;
    if (s_in_read || !s_wr_open) return;
    s_wr_open = false;
    if (s_wr_bad) { s_discards++; return; }
    for (i = 0u; i < s_wr_len; i++) {
        uint8_t r = (uint8_t)(s_wr_first + i);
        uint8_t b = s_wr_buf[r - HTR_CTRL_FIRST];
        s_stage[r] = b;
        s_dirty[r] = 1u;
        if (((r == HTR_R_RUN_REQUEST) && (b == 0u)) || ((r == HTR_R_COMMAND) && (b == HTR_CMD_STOP))) {
            s_stop_seen = true;
        }
    }
    if (s_wr_len != 0u) s_commit = true;
}

void htr_regs_i2c_error(void)
{
    wr_drop();
}

/* ---------------- superloop side ---------------- */
static bool known_type(uint8_t t)
{
    return (t == HTR_TYPE_AUTO) || (t == HTR_TYPE_NONE) ||
           ((t < 16u) && ((s_cfg.supported_types >> t) & 1u));
}

static void persist(void)
{
    if (s_cfg.nv != NULL) s_cfg.nv->save(s_cfg.nv->ctx, s_type_setting, HTR_NV_LEGACY_TIMEOUT_MIN);
}

static bool host_live(uint32_t now)
{
    return s_hb_have && ((uint32_t)(now - s_host_last) < (uint32_t)s_timeout_s * 1000u);
}

/* A stop the host asked for: always accepted, and it arms the next start. */
static void clean_stop(void)
{
    s_rq.run = 0u;
    s_armed = true;
    s_refused = false;
    s_host_stop = false;
}

static void keyed_start(uint32_t now)
{
    uint8_t res = HTR_RES_OK;
    if (s_rq.run) return;                         /* host re-sending its block while on */
    if (s_type_setting == HTR_TYPE_NONE) res = HTR_RES_NO_TYPE;
    else if (!host_live(now)) res = HTR_RES_NO_HOST;
    else if (!s_armed) res = HTR_RES_NOT_ARMED;
    s_cmd_result = res;
    if (res != HTR_RES_OK) { s_refused = true; return; }
    s_rq.run = 1u;
    s_armed = false;
    s_refused = false;
}

static void run_command(uint8_t cmd, uint32_t now)
{
    switch (cmd) {
    case HTR_CMD_START:                           /* map v2: RUN_REQUEST is the only start */
        s_cmd_result = HTR_RES_NOT_SUPPORTED; return;
    case HTR_CMD_STOP:
        clean_stop(); s_cmd_result = HTR_RES_OK; return;
    case HTR_CMD_CLEAR_FAULT:
        s_cmd_result = htr_fsm_clear_fault(now); return;
    case HTR_CMD_REINIT_IFACE:
        s_cmd_result = htr_fsm_reinit_iface(now); return;
    case HTR_CMD_AUTO_DETECT:
        s_cmd_result = htr_fsm_redetect(now); return;
    default:
        s_cmd_result = HTR_RES_BAD_ARG; return;
    }
}

static void apply_writes(const uint8_t *st, const uint8_t *dt, bool stop_seen, uint32_t now)
{
    /* Heartbeat and timeout first: a block carrying the key and a fresh
       heartbeat starts in one transaction. */
    if (dt[HTR_R_HOST_HEARTBEAT]) {
        uint8_t hb = st[HTR_R_HOST_HEARTBEAT];
        if (!s_hb_have || (hb != s_hb_last)) { s_hb_have = true; s_hb_last = hb; s_host_last = now; }
    }
    if (dt[HTR_R_HOST_TIMEOUT_S]) {
        uint8_t to = st[HTR_R_HOST_TIMEOUT_S];
        if ((to < HTR_HOST_TIMEOUT_MIN_S) || (to > HTR_HOST_TIMEOUT_MAX_S)) s_cmd_result = HTR_RES_BAD_ARG;
        else s_timeout_s = to;
    }
    if (stop_seen) clean_stop();              /* then whatever the host wrote after it */
    if (dt[HTR_R_HEATER_TYPE]) {
        uint8_t t = st[HTR_R_HEATER_TYPE];
        if (!known_type(t)) s_cmd_result = HTR_RES_NOT_SUPPORTED;
        else if (t != s_type_setting) {
            s_cmd_result = htr_fsm_set_type(t, now);
            if (s_cmd_result == HTR_RES_OK) { s_type_setting = t; persist(); }
        }
    }
    if (dt[HTR_R_RUN_REQUEST]) {
        uint8_t run = st[HTR_R_RUN_REQUEST];
        if (run == 0u) clean_stop();
        else if (run == HTR_RUN_KEY) keyed_start(now);
        else { s_rq.run = 0u; s_cmd_result = HTR_RES_BAD_ARG; }   /* garbage stops, never arms */
    }
    if (dt[HTR_R_LEVEL]) {
        if ((st[HTR_R_LEVEL] < 1u) || (st[HTR_R_LEVEL] > 10u)) s_cmd_result = HTR_RES_BAD_ARG;
        else s_rq.level = st[HTR_R_LEVEL];
    }
    if (dt[HTR_R_SETPOINT_C]) {
        int8_t sp = (int8_t)st[HTR_R_SETPOINT_C];
        if ((sp < 5) || (sp > 30)) s_cmd_result = HTR_RES_BAD_ARG;
        else s_rq.setpoint_c = sp;
    }
    if (dt[HTR_R_AMBIENT_C_X10] && dt[HTR_R_AMBIENT_C_X10 + 1u]) {
        int16_t a = (int16_t)(uint16_t)(st[HTR_R_AMBIENT_C_X10] | ((uint16_t)st[HTR_R_AMBIENT_C_X10 + 1u] << 8));
        if ((a == HTR_AMBIENT_UNKNOWN) || ((a >= -400) && (a <= 850))) s_rq.ambient_c_x10 = a;
        else s_cmd_result = HTR_RES_BAD_ARG;
    } else if (dt[HTR_R_AMBIENT_C_X10] || dt[HTR_R_AMBIENT_C_X10 + 1u]) {
        s_cmd_result = HTR_RES_BAD_ARG;             /* half an s16 is never applied */
    }
    if (dt[HTR_R_COMMAND]) run_command(st[HTR_R_COMMAND], now);
}

void htr_regs_poll(uint32_t now)
{
    uint8_t st[HTR_REG_COUNT];
    uint8_t dt[HTR_REG_COUNT];
    bool commit;
    bool stop_seen = false;
    bool live;
    lock();
    commit = s_commit;
    if (commit) {
        memcpy(st, s_stage, sizeof(st));
        memcpy(dt, s_dirty, sizeof(dt));
        memset(s_dirty, 0, sizeof(s_dirty));
        s_commit = false;
        stop_seen = s_stop_seen;
        s_stop_seen = false;
    }
    unlock();

    if (commit) apply_writes(st, dt, stop_seen, now);

    /* Host timeout (D2): controlled stop through the normal cooldown path.
       Losing the host also disarms, so its old block can't restart us. */
    live = host_live(now);
    if (s_host_live && !live) {
        s_armed = false;
        if (s_rq.run) { s_rq.run = 0u; s_host_stop = true; }
    }
    s_host_live = live;
    htr_fsm_set_request(&s_rq);
}

void htr_regs_publish(uint32_t now, uint32_t echo_errs)
{
    htr_fsm_view_t v;
    uint8_t m[HTR_REG_COUNT];
    uint16_t flags;
    uint32_t age;
    uint16_t discards;

    htr_fsm_view(now, &v);
    if (s_rq.run && !v.rq.run) s_armed = false;   /* FAULT dropped run: not a host stop */
    s_rq.run = v.rq.run;                     /* FAULT forces run to 0: reflect it */
    if (echo_errs != s_prev_echo) { s_prev_echo = echo_errs; s_echo_tick = now; s_echo_seen = true; }

    flags = v.flags;
    if (s_host_stop) flags |= HTR_F_HOST_TIMEOUT_STOP;
    if (host_live(now)) flags |= HTR_F_HOST_LIVE;
    if (s_armed) flags |= HTR_F_START_ARMED;
    if (s_refused) flags |= HTR_F_START_REFUSED;
    if (s_echo_seen && ((uint32_t)(now - s_echo_tick) <= ECHO_STICKY_MS)) flags |= HTR_F_ECHO_FAULT;
    age = v.st.any_rx ? (uint32_t)(now - v.st.last_rx_tick) : 0xFFFFFFFFu;

    memset(&m[HTR_STATUS_FIRST], 0, HTR_STATUS_LEN);
    m[HTR_R_PHASE] = v.phase;
    m[HTR_R_ACTIVE_TYPE] = v.active_type;
    put16(m, HTR_R_FLAGS, flags);
    put16(m, HTR_R_VALID, v.st.valid);
    m[HTR_R_VENDOR_STATE] = v.st.vendor_state;
    m[HTR_R_VENDOR_SUB] = v.st.vendor_sub;
    m[HTR_R_FAULT_CODE] = v.st.fault_code;
    m[HTR_R_ACTIVE_LEVEL] = v.st.active_level;
    put16(m, HTR_R_SUPPLY_MV, v.st.supply_mv);
    put16(m, HTR_R_FAN_RPM, v.st.fan_rpm);
    put16(m, HTR_R_PUMP_HZ_X10, v.st.pump_hz_x10);
    put16(m, HTR_R_EXCH_RAW, v.st.exch_raw);
    put16(m, HTR_R_EXCH_C_X10, (uint16_t)v.st.exch_c_x10);
    put16(m, HTR_R_STATE_SECONDS, v.st.state_seconds);
    put16(m, HTR_R_AGE_MS, sat16(age));
    put16(m, HTR_R_VALID_FRAMES, (uint16_t)(v.st.valid_frames & 0xFFFFu));
    put16(m, HTR_R_CRC_FAILS, sat16(v.st.crc_fails));
    put16(m, HTR_R_TRANSPORT_ERRS, sat16(v.st.transport_errs));
    put16(m, HTR_R_ECHO_ERRS, sat16(echo_errs));
    put16(m, HTR_R_CAPS, v.caps);
    put16(m, HTR_R_HOST_IDLE_S, sat16((uint32_t)(now - s_host_last) / 1000u));
    m[HTR_R_LINE_SENSE] = v.line_sense;
    m[HTR_R_DETECT_RESULT] = v.detect_result;

    lock();
    discards = s_discards;
    put16(m, HTR_R_WRITES_DISCARDED, discards);
    fill_control(s_live);
    memcpy(&s_live[HTR_STATUS_FIRST], &m[HTR_STATUS_FIRST], HTR_STATUS_LEN);
    unlock();
}
