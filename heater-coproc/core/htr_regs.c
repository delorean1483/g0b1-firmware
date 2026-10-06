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
/* staged master writes, consumed by htr_regs_poll() */
static uint8_t s_stage[HTR_REG_COUNT];
static uint8_t s_dirty[HTR_REG_COUNT];
static volatile bool s_commit;
static volatile bool s_host_seen;
/* ISR transaction state */
static bool s_in_read;
static bool s_have_ptr;
static uint8_t s_ptr;
/* control state owned by the superloop */
static htr_request_t s_rq;
static uint8_t s_type_setting;
static uint8_t s_timeout_min;
static uint8_t s_cmd_result;
static uint32_t s_host_last;
static bool s_host_stop;
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
    m[HTR_R_RUN_REQUEST] = s_rq.run;
    m[HTR_R_LEVEL] = s_rq.level;
    m[HTR_R_SETPOINT_C] = (uint8_t)s_rq.setpoint_c;
    put16(m, HTR_R_AMBIENT_C_X10, (uint16_t)s_rq.ambient_c_x10);
    m[HTR_R_HOST_TIMEOUT] = s_timeout_min;
    m[HTR_R_COMMAND] = 0u;
    m[HTR_R_CMD_RESULT] = s_cmd_result;
}

uint8_t htr_regs_init(const htr_regs_cfg_t *cfg, uint32_t now)
{
    s_cfg = *cfg;
    memset(s_live, 0, sizeof(s_live));
    memset(s_dirty, 0, sizeof(s_dirty));
    s_commit = false;
    s_host_seen = false;
    s_in_read = false;
    s_have_ptr = false;
    s_ptr = 0u;
    s_rq.run = 0u;
    s_rq.level = 1u;
    s_rq.setpoint_c = 20;
    s_rq.ambient_c_x10 = HTR_AMBIENT_UNKNOWN;
    s_cmd_result = HTR_RES_OK;
    s_host_last = now;
    s_host_stop = false;
    s_prev_echo = 0u;
    s_echo_seen = false;
    if ((cfg->nv == NULL) || !cfg->nv->load(cfg->nv->ctx, &s_type_setting, &s_timeout_min) ||
        (s_timeout_min > 60u)) {
        s_type_setting = HTR_NV_DEFAULT_TYPE;
        s_timeout_min = HTR_NV_DEFAULT_TIMEOUT;
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

uint8_t htr_regs_host_timeout_min(void) { return s_timeout_min; }

/* ---------------- ISR side ---------------- */
void htr_regs_i2c_begin(bool is_read)
{
    s_host_seen = true;
    s_in_read = is_read;
    if (is_read) {
        memcpy(s_snap, s_live, sizeof(s_snap));   /* one consistent image per read */
    } else {
        s_have_ptr = false;
    }
}

void htr_regs_i2c_rx(uint8_t byte)
{
    if (!s_have_ptr) { s_ptr = byte; s_have_ptr = true; return; }
    if (s_ptr < HTR_REG_COUNT) {
        s_stage[s_ptr] = byte;
        s_dirty[s_ptr] = 1u;
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
    if (!s_in_read) s_commit = true;
}

/* ---------------- superloop side ---------------- */
static bool known_type(uint8_t t)
{
    return (t == HTR_TYPE_AUTO) || (t == HTR_TYPE_NONE) ||
           ((t < 16u) && ((s_cfg.supported_types >> t) & 1u));
}

static void persist(void)
{
    if (s_cfg.nv != NULL) s_cfg.nv->save(s_cfg.nv->ctx, s_type_setting, s_timeout_min);
}

static void run_command(uint8_t cmd, uint32_t now)
{
    switch (cmd) {
    case HTR_CMD_START:
        if (s_type_setting == HTR_TYPE_NONE) { s_cmd_result = HTR_RES_NO_TYPE; return; }
        s_rq.run = 1u; s_host_stop = false; s_cmd_result = HTR_RES_OK; return;
    case HTR_CMD_STOP:
        s_rq.run = 0u; s_cmd_result = HTR_RES_OK; return;
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

static void apply_writes(const uint8_t *st, const uint8_t *dt, uint32_t now)
{
    if (dt[HTR_R_HEATER_TYPE]) {
        uint8_t t = st[HTR_R_HEATER_TYPE];
        if (!known_type(t)) s_cmd_result = HTR_RES_NOT_SUPPORTED;
        else if (t != s_type_setting) {
            s_cmd_result = htr_fsm_set_type(t, now);
            if (s_cmd_result == HTR_RES_OK) { s_type_setting = t; persist(); }
        }
    }
    if (dt[HTR_R_RUN_REQUEST]) {
        if (st[HTR_R_RUN_REQUEST] > 1u) s_cmd_result = HTR_RES_BAD_ARG;
        else { s_rq.run = st[HTR_R_RUN_REQUEST]; s_host_stop = false; }
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
    if (dt[HTR_R_HOST_TIMEOUT]) {
        if (st[HTR_R_HOST_TIMEOUT] > 60u) s_cmd_result = HTR_RES_BAD_ARG;
        else if (st[HTR_R_HOST_TIMEOUT] != s_timeout_min) { s_timeout_min = st[HTR_R_HOST_TIMEOUT]; persist(); }
    }
    if (dt[HTR_R_COMMAND]) run_command(st[HTR_R_COMMAND], now);
}

void htr_regs_poll(uint32_t now)
{
    uint8_t st[HTR_REG_COUNT];
    uint8_t dt[HTR_REG_COUNT];
    bool commit;
    lock();
    commit = s_commit;
    if (commit) {
        memcpy(st, s_stage, sizeof(st));
        memcpy(dt, s_dirty, sizeof(dt));
        memset(s_dirty, 0, sizeof(s_dirty));
        s_commit = false;
    }
    if (s_host_seen) { s_host_seen = false; s_host_last = now; }
    unlock();

    if (commit) apply_writes(st, dt, now);

    /* Host timeout (D2): controlled stop through the normal cooldown path. */
    if (s_rq.run && (s_timeout_min != 0u) &&
        ((uint32_t)(now - s_host_last) >= (uint32_t)s_timeout_min * 60000u)) {
        s_rq.run = 0u;
        s_host_stop = true;
    }
    htr_fsm_set_request(&s_rq);
}

void htr_regs_publish(uint32_t now, uint32_t echo_errs)
{
    htr_fsm_view_t v;
    uint8_t m[HTR_REG_COUNT];
    uint16_t flags;
    uint32_t age;

    htr_fsm_view(now, &v);
    s_rq.run = v.rq.run;                     /* FAULT forces run to 0: reflect it */
    if (echo_errs != s_prev_echo) { s_prev_echo = echo_errs; s_echo_tick = now; s_echo_seen = true; }

    flags = v.flags;
    if (s_host_stop) flags |= HTR_F_HOST_TIMEOUT_STOP;
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
    fill_control(s_live);
    memcpy(&s_live[HTR_STATUS_FIRST], &m[HTR_STATUS_FIRST], HTR_STATUS_LEN);
    unlock();
}
