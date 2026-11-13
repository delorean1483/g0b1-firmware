#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include <string.h>

static const i2c_backend_t *s_bus;
static void (*s_reset)(void);
static uint8_t s_request;
static bool s_cortex_seen, s_silence_stop;
static uint32_t s_cortex_at;
static uint32_t (*s_poll_clock)(void);
static uint16_t s_poll_head, s_poll_log[HEATER_POLL_LOG_SLOTS][HEATER_POLL_LOG_WORDS];
static uint32_t s_last_valid_poll, s_max_poll_gap;
static bool s_valid_poll_seen;

void heater_set_poll_clock(uint32_t (*clock_ms)(void)) { s_poll_clock = clock_ms; }

uint16_t heater_poll_diagnostic(uint16_t reg)
{
    if (reg == HEATER_POLL_LOG_HEAD) return s_poll_head;
    if (reg == HEATER_POLL_MAX_GAP_LO) return (uint16_t)s_max_poll_gap;
    if (reg == HEATER_POLL_MAX_GAP_HI) return (uint16_t)(s_max_poll_gap >> 16);
    unsigned offset = reg - HEATER_POLL_LOG_BASE;
    return s_poll_log[offset / HEATER_POLL_LOG_WORDS][offset % HEATER_POLL_LOG_WORDS];
}

static void log_poll(uint32_t due, uint32_t start, uint16_t flags)
{
    uint32_t end = s_poll_clock ? s_poll_clock() : start;
    unsigned slot = s_poll_head++ % HEATER_POLL_LOG_SLOTS;
    uint16_t *entry = s_poll_log[slot];
    entry[0] = s_poll_head;
    entry[1] = (uint16_t)due;
    entry[2] = (uint16_t)(due >> 16);
    entry[3] = (uint16_t)start;
    entry[4] = (uint16_t)(start >> 16);
    entry[5] = (uint16_t)((end - start) > 0xFFFFu ? 0xFFFFu : end - start);
    entry[6] = flags;
    if (flags & HEATER_POLL_VALID_STATUS) {
        uint32_t gap = end - s_last_valid_poll;
        if (s_valid_poll_seen && gap > s_max_poll_gap) s_max_poll_gap = gap;
        s_valid_poll_seen = true;
        s_last_valid_poll = end;
    }
}
static uint8_t  s_level = 1U;
static int8_t   s_setpoint = 20;
static int16_t  s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
static uint8_t  s_type_select = HTR_TYPE_AUTO;
static uint8_t  s_type_pending;
static uint8_t  s_coproc_type = HTR_TYPE_AUTO;   /* HEATER_TYPE as read back */
static uint8_t s_identified;
static uint32_t (*s_recovery_epoch)(void);
static uint32_t s_checked_epoch;
static uint16_t s_map_fault;
static bool s_status_seen;


static uint8_t  s_ever_identified;   /* NACKs only count as transport faults after this */
static uint8_t  s_cmd_pending;       /* COMMAND to send once (0 = none) */
static uint32_t s_last_poll, s_last_ok, s_last_xport;
static uint8_t  s_xport_seen;
static uint32_t s_xport_errors;
static heater_telemetry_t s_t;

static uint16_t le16(const uint8_t *b, uint8_t reg) { return (uint16_t)(b[reg] | ((uint16_t)b[reg + 1U] << 8)); }
static uint16_t sat16(uint32_t v) { return (v > 0xFFFFU) ? 0xFFFFU : (uint16_t)v; }

void heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now)
{
    s_bus = bus;
    s_cortex_seen = s_silence_stop = false;
    s_cortex_at = now;
    s_poll_clock = NULL;
    s_poll_head = 0;
    s_max_poll_gap = 0;
    s_last_valid_poll = now;
    s_valid_poll_seen = false;
    memset(s_poll_log, 0, sizeof s_poll_log);
    s_reset = reset_coproc;
    s_request = 0U;
    s_level = 1U;
    s_setpoint = 20;
    s_ambient_x10 = HTR_AMBIENT_UNKNOWN;
    s_type_select = HTR_TYPE_AUTO;
    s_type_pending = 0U;
    s_coproc_type = HTR_TYPE_AUTO;
    s_identified = 0U;
    s_recovery_epoch = NULL;
    s_checked_epoch = 0U;
    s_map_fault = 0U;
    s_status_seen = false;
    s_ever_identified = 0U;
    s_cmd_pending = 0U;
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

void heater_set_recovery_epoch(uint32_t (*epoch)(void))
{
    s_recovery_epoch = epoch;
    s_checked_epoch = epoch ? epoch() : 0U;
}

static bool recovery_changed(void)
{
    return s_recovery_epoch && s_recovery_epoch() != s_checked_epoch;
}

static void close_write_gate(uint16_t fault)
{
    s_identified = 0U;
    s_status_seen = false;
    s_map_fault = fault;
    if (fault == HEATER_FLAG_MAP_MISMATCH) s_request = 0U;
    s_cmd_pending = 0U;
    s_type_pending = 0U;
}

static void latch_cortex_silence(void)
{
    s_silence_stop = true;
    s_request = 0U;
    s_cmd_pending = 0U;
    s_type_pending = 0U;
    s_identified = 0U;
    s_status_seen = false;
}

void heater_note_cortex_traffic(uint32_t now)
{
    if (s_cortex_seen && (uint32_t)(now - s_cortex_at) >= HEATER_CORTEX_FRESH_MS)
        latch_cortex_silence();
    s_cortex_seen = true;
    s_cortex_at = now;
}

static bool check_map(uint32_t now)
{
    uint8_t id[2];
    if (s_bus->read(s_bus->ctx, HTR_R_DEVICE_ID, id, sizeof id) != 0) {
        close_write_gate(HEATER_FLAG_MAP_UNREADABLE);
        if (s_ever_identified) xport_fail(now);
        return false;
    }
    if (recovery_changed()) close_write_gate(0U);
    s_checked_epoch = s_recovery_epoch ? s_recovery_epoch() : 0U;
    if (id[0] != HTR_DEVICE_ID || id[1] != HEATER_EXPECTED_MAP_VERSION) {
        close_write_gate(HEATER_FLAG_MAP_MISMATCH);
        return false;
    }
    s_identified = s_ever_identified = 1U;
    s_map_fault = 0U;
    return true;
}

/* All service writes go through this gate. The hardware backend also stops a
   write if its own bus preparation performs a recovery before reaching HAL. */
static int write_checked(uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (!s_identified || recovery_changed()) {
        close_write_gate(0U);
        return -1;
    }
    int result = s_bus->write(s_bus->ctx, reg, data, len);
    if (recovery_changed()) {
        close_write_gate(0U);
        return -1;
    }
    return result;
}

void heater_process(uint32_t now)
{
    uint8_t m[HTR_REG_COUNT];
    uint8_t ctl[5];
    if (s_bus == NULL) return;

    bool logged = false;
    uint16_t poll_flags = 0U;
    uint32_t due = now;
    /* v1 has no heartbeat register: even a read refreshes its watchdog.
       Cease addressed traffic on silence so the local timeout can expire. */
    if (!s_cortex_seen || (uint32_t)(now - s_cortex_at) >= HEATER_CORTEX_FRESH_MS) {
        if (s_cortex_seen) latch_cortex_silence();
        goto publish;
    }
    bool recovered = recovery_changed();
    if (recovered) {
        close_write_gate(0U);
        s_checked_epoch = s_recovery_epoch();
    }
    uint32_t interval = s_identified ? HEATER_POLL_MS : HEATER_IDENT_RETRY_MS;
    if (!recovered && (uint32_t)(now - s_last_poll) < interval) goto publish;
    bool previously_identified = s_identified != 0U;
    due = s_last_poll + interval;
    logged = true;
    s_last_poll = now;
    /* The v1 status block starts at 0x10, so identity is read separately. */
    if (!check_map(now)) {
        if (s_map_fault == HEATER_FLAG_MAP_UNREADABLE) goto lost_check;
        goto publish;
    }
    if (s_bus->read(s_bus->ctx, HTR_R_HEATER_TYPE, &m[HTR_R_HEATER_TYPE],
                    (uint16_t)(HTR_REG_COUNT - HTR_R_HEATER_TYPE)) != 0 || recovery_changed()) {
        close_write_gate(HEATER_FLAG_MAP_UNREADABLE);
        xport_fail(now);
        goto lost_check;
    }

    uint16_t frames = le16(m, HTR_R_VALID_FRAMES);
    bool reset_hint = s_status_seen &&
        ((m[HTR_R_PHASE] == HTR_PHASE_DETECTING && s_t.phase != HTR_PHASE_DETECTING) ||
         (frames < s_t.valid_frames && (uint16_t)(frames - s_t.valid_frames) >= 0x8000u));
    if (reset_hint) {
        s_request = 0U;
        close_write_gate(HEATER_FLAG_MAP_UNREADABLE);
        if (!check_map(now)) goto lost_check;
    }
    s_coproc_type = m[HTR_R_HEATER_TYPE];
    s_t.cmd_result = m[HTR_R_CMD_RESULT];
    decode_status(m);
    s_status_seen = true;
    if (s_t.phase == HTR_PHASE_FAULT) {
        s_request = 0U;
        if (s_cmd_pending == HTR_CMD_START) s_cmd_pending = 0U;
    }
    if (s_silence_stop && m[HTR_R_RUN_REQUEST] == 0U) s_silence_stop = false;
    poll_flags |= HEATER_POLL_VALID_STATUS;
    if (!previously_identified && !s_silence_stop) {
        s_last_ok = now;
        goto publish;
    }

    ctl[0] = s_silence_stop ? 0U : s_request;
    ctl[1] = s_level;
    ctl[2] = (uint8_t)s_setpoint;
    ctl[3] = (uint8_t)((uint16_t)s_ambient_x10 & 0xFFU);
    ctl[4] = (uint8_t)((uint16_t)s_ambient_x10 >> 8);
    /* Compare the prior write against the next fresh status snapshot.
       Keep RUN_REQUEST out of level/setpoint writes and ambient atomic. */
    if (ctl[0] != m[HTR_R_RUN_REQUEST] &&
        write_checked(HTR_R_RUN_REQUEST, ctl, 1U) != 0) goto write_failed;
    if (memcmp(&ctl[1], &m[HTR_R_LEVEL], 4U) != 0 &&
        write_checked(HTR_R_LEVEL, &ctl[1], 4U) != 0) goto write_failed;
    if (s_type_pending) {
        if (s_type_select == m[HTR_R_HEATER_TYPE]) s_type_pending = 0U;
        else if (write_checked(HTR_R_HEATER_TYPE, &s_type_select, 1U) != 0) goto write_failed;
    }
    if (s_cmd_pending != 0U) {
        /* Consume before attempting: a timeout does not prove non-delivery.
         * A command must not be replayed by the next service poll either. */
        uint8_t command = s_cmd_pending;
        s_cmd_pending = 0U;
        if (write_checked(HTR_R_COMMAND, &command, 1U) != 0) goto write_failed;
    }
    s_last_ok = now;
    goto publish;

write_failed:
    poll_flags |= HEATER_POLL_WRITE_FAILED;
    xport_fail(now);
lost_check:
    if (s_ever_identified && (uint32_t)(now - s_last_ok) >= HEATER_LOST_MS) {
        s_request = 0U;
        close_write_gate(HEATER_FLAG_MAP_UNREADABLE);
        s_last_ok = now;
        if (s_reset != NULL) s_reset();
    }
publish:
    if (logged) {
        if (recovered || recovery_changed()) poll_flags |= HEATER_POLL_RECOVERY;
        log_poll(due, now, poll_flags);
    }
    s_t.present = s_identified;
    s_t.flags = (uint16_t)((s_t.flags & ~(HEATER_FLAG_MAP_MISMATCH | HEATER_FLAG_MAP_UNREADABLE)) |
                          s_map_fault);
    s_t.target_level = s_level;
    s_t.transport_errors = sat16(s_xport_errors);
    if (s_xport_seen && ((uint32_t)(now - s_last_xport) <= HEATER_XPORT_STICKY_MS)) {
        s_t.flags |= HEATER_FLAG_XPORT_FAULT;
    } else {
        s_t.flags &= (uint16_t)~HEATER_FLAG_XPORT_FAULT;
    }
    if (s_identified == 0U) {
        s_t.flags &= (uint16_t)~(HEATER_FLAG_FRESH | HEATER_FLAG_SAFE_OFF);
        if (s_map_fault == HEATER_FLAG_MAP_MISMATCH || s_ever_identified)
            s_t.flags |= HEATER_FLAG_COMMS_FAULT;
    }
}

void    heater_set_request(uint8_t on) { s_request = (on != 0U && !s_silence_stop && s_map_fault != HEATER_FLAG_MAP_MISMATCH) ? 1U : 0U; }
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
bool heater_send_command(uint8_t cmd)
{
    if ((cmd < HTR_CMD_START) || (cmd > HTR_CMD_AUTO_DETECT)) return false;
    if (cmd == HTR_CMD_START && (s_silence_stop || s_map_fault != 0U)) return false;
    s_cmd_pending = cmd;
    return true;
}
void heater_get_telemetry(heater_telemetry_t *out) { if (out != NULL) *out = s_t; }
