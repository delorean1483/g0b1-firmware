#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include <string.h>

static const i2c_backend_t *s_bus;
static void (*s_reset)(void);
static uint8_t  s_request;
static uint8_t  s_level = 1U;
static int8_t   s_setpoint = 20;
static int16_t  s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
static uint8_t  s_type_select = HTR_TYPE_AUTO;
static uint8_t  s_type_pending;
static uint8_t  s_coproc_type = HTR_TYPE_AUTO;   /* HEATER_TYPE as read back */
static uint8_t  s_identified;
static uint32_t s_last_poll, s_last_ok, s_last_xport;
static uint8_t  s_xport_seen;
static uint32_t s_xport_errors;
static heater_telemetry_t s_t;

static uint16_t le16(const uint8_t *b, uint8_t reg) { return (uint16_t)(b[reg] | ((uint16_t)b[reg + 1U] << 8)); }
static uint16_t sat16(uint32_t v) { return (v > 0xFFFFU) ? 0xFFFFU : (uint16_t)v; }

void heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now)
{
    s_bus = bus;
    s_reset = reset_coproc;
    s_request = 0U;
    s_level = 1U;
    s_setpoint = 20;
    s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
    s_type_select = HTR_TYPE_AUTO;
    s_type_pending = 0U;
    s_coproc_type = HTR_TYPE_AUTO;
    s_identified = 0U;
    s_last_poll = now - HEATER_IDENT_RETRY_MS;
    s_last_ok = now;
    s_xport_seen = 0U;
    s_xport_errors = 0U;
    memset(&s_t, 0, sizeof(s_t));
    s_t.target_level = 1U;
    s_t.age_ms = 0xFFFFU;
}

static void xport_fail(uint32_t now)
{
    s_xport_errors++;
    s_xport_seen = 1U;
    s_last_xport = now;
}

static uint8_t legacy_state(uint8_t phase)
{
    switch (phase) {
    case HTR_PHASE_STARTING:       return 1U;
    case HTR_PHASE_RUNNING:        return 3U;
    case HTR_PHASE_STOP_REQUESTED:
    case HTR_PHASE_COOLDOWN:       return 4U;
    default:                       return 0U;   /* OFF, DETECTING, FAULT */
    }
}

static void decode_status(const uint8_t *m)
{
    uint16_t f = le16(m, HTR_R_FLAGS);
    uint16_t legacy = 0U;
    s_t.phase = m[HTR_R_PHASE];
    s_t.state = legacy_state(s_t.phase);
    s_t.type = m[HTR_R_ACTIVE_TYPE];
    s_t.active_level = m[HTR_R_ACTIVE_LEVEL];
    s_t.error = m[HTR_R_FAULT_CODE];
    if ((s_t.phase == HTR_PHASE_FAULT) && (s_t.error == 0U)) s_t.error = 0xFFU;
    s_t.supply_mv = le16(m, HTR_R_SUPPLY_MV);
    s_t.fan_rpm = le16(m, HTR_R_FAN_RPM);
    s_t.pump_hz_x10 = le16(m, HTR_R_PUMP_HZ_X10);
    s_t.exchanger_raw = le16(m, HTR_R_EXCH_RAW);
    s_t.state_seconds = le16(m, HTR_R_STATE_SECONDS);
    s_t.age_ms = le16(m, HTR_R_AGE_MS);
    s_t.valid_frames = le16(m, HTR_R_VALID_FRAMES);
    s_t.checksum_failures = le16(m, HTR_R_CRC_FAILS);
    s_t.caps = le16(m, HTR_R_CAPS);
    s_t.vendor_state = (uint16_t)(((uint16_t)m[HTR_R_VENDOR_STATE] << 8) | m[HTR_R_VENDOR_SUB]);
    s_t.detect = (uint16_t)(((uint16_t)m[HTR_R_DETECT_RESULT] << 8) | m[HTR_R_LINE_SENSE]);
    if (f & HTR_F_FRESH) legacy |= HEATER_FLAG_FRESH;
    if ((s_t.phase == HTR_PHASE_STOP_REQUESTED) || (s_t.phase == HTR_PHASE_COOLDOWN)) legacy |= HEATER_FLAG_COOLDOWN;
    if (f & HTR_F_SAFE_OFF) legacy |= HEATER_FLAG_SAFE_OFF;
    if (f & HTR_F_COMMS_FAULT) legacy |= HEATER_FLAG_COMMS_FAULT;
    s_t.flags = legacy;
}

static int identify(void)
{
    uint8_t id[2];
    if (s_bus->read(s_bus->ctx, HTR_R_DEVICE_ID, id, 2U) != 0) return -1;
    return ((id[0] == HTR_DEVICE_ID) && (id[1] == HTR_MAP_VERSION)) ? 1 : 0;
}

void heater_process(uint32_t now)
{
    uint8_t m[HTR_REG_COUNT];
    uint8_t ctl[5];
    int r;
    if (s_bus == NULL) return;

    if (s_identified == 0U) {
        if ((uint32_t)(now - s_last_poll) < HEATER_IDENT_RETRY_MS) goto publish;
        s_last_poll = now;
        r = identify();
        if (r < 0) { xport_fail(now); goto publish; }
        if (r == 0) goto publish;            /* wrong device / map: never control it */
        s_identified = 1U;
        s_last_ok = now;
    }
    if ((uint32_t)(now - s_last_poll) < HEATER_POLL_MS) goto publish;
    s_last_poll = now;

    ctl[0] = s_request;
    ctl[1] = s_level;
    ctl[2] = (uint8_t)s_setpoint;
    ctl[3] = (uint8_t)((uint16_t)s_ambient_x10 & 0xFFU);
    ctl[4] = (uint8_t)((uint16_t)s_ambient_x10 >> 8);
    if (s_bus->write(s_bus->ctx, HTR_R_RUN_REQUEST, ctl, sizeof(ctl)) != 0) { xport_fail(now); goto lost_check; }
    if (s_type_pending) {
        if (s_bus->write(s_bus->ctx, HTR_R_HEATER_TYPE, &s_type_select, 1U) != 0) { xport_fail(now); goto lost_check; }
        s_type_pending = 0U;
    }
    /* One read covers control readback (0x10..) and the status block. */
    if (s_bus->read(s_bus->ctx, HTR_R_HEATER_TYPE, &m[HTR_R_HEATER_TYPE],
                    (uint16_t)(HTR_REG_COUNT - HTR_R_HEATER_TYPE)) != 0) {
        xport_fail(now);
        goto lost_check;
    }
    s_last_ok = now;
    s_coproc_type = m[HTR_R_HEATER_TYPE];
    decode_status(m);
    goto publish;

lost_check:
    if ((uint32_t)(now - s_last_ok) >= HEATER_LOST_MS) {
        s_identified = 0U;                    /* re-identify; optional hard reset */
        s_last_ok = now;
        if (s_reset != NULL) s_reset();
    }
publish:
    s_t.present = s_identified;
    s_t.target_level = s_level;
    s_t.transport_errors = sat16(s_xport_errors);
    if (s_xport_seen && ((uint32_t)(now - s_last_xport) <= HEATER_XPORT_STICKY_MS)) {
        s_t.flags |= HEATER_FLAG_XPORT_FAULT;
    } else {
        s_t.flags &= (uint16_t)~HEATER_FLAG_XPORT_FAULT;
    }
    if (s_identified == 0U) {
        s_t.flags &= (uint16_t)~(HEATER_FLAG_FRESH | HEATER_FLAG_SAFE_OFF);
        if (s_request) s_t.flags |= HEATER_FLAG_COMMS_FAULT;
    }
}

void    heater_set_request(uint8_t on) { s_request = (on != 0U) ? 1U : 0U; }
uint8_t heater_get_request(void) { return s_request; }
void    heater_set_level(uint8_t level) { if ((level >= 1U) && (level <= 10U)) s_level = level; }
uint8_t heater_get_level(void) { return s_level; }
void    heater_set_setpoint_c(int8_t c) { if ((c >= 5) && (c <= 30)) s_setpoint = c; }
int8_t  heater_get_setpoint_c(void) { return s_setpoint; }
void    heater_set_type_select(uint8_t type)
{
    s_type_select = type;      /* explicit Modbus write: always forwarded once */
    s_type_pending = 1U;
}
/* The coprocessor persists HEATER_TYPE, so report what it holds once known. */
uint8_t heater_get_type_select(void) { return s_identified ? s_coproc_type : s_type_select; }
void    heater_set_ambient_f(int16_t deg_f)
{
    /* (F - 32) * 5/9, in tenths, rounded */
    int32_t x10 = ((int32_t)deg_f - 32) * 50;
    s_ambient_x10 = (int16_t)((x10 >= 0) ? ((x10 + 4) / 9) : ((x10 - 4) / 9));
}
void heater_get_telemetry(heater_telemetry_t *out) { if (out != NULL) *out = s_t; }
