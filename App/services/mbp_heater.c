#include "mbp_heater.h"
#include "mb_regmodel.h"
#include "heater.h"

static heater_telemetry_t tel(void) { heater_telemetry_t t; heater_get_telemetry(&t); return t; }

static modbus_exc_t rd_request(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_request(); return MB_EXC_NONE; }
static modbus_exc_t wr_request(uint16_t reg, uint16_t v) {
    (void)reg;
    if (v > 1U) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_request((uint8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_level(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_level(); return MB_EXC_NONE; }
static modbus_exc_t wr_level(uint16_t reg, uint16_t v) {
    (void)reg;
    if ((v < 1U) || (v > 10U)) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_level((uint8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_setpoint(uint16_t reg, uint16_t *o) { (void)reg; *o = (uint16_t)(int16_t)heater_get_setpoint_c(); return MB_EXC_NONE; }
static modbus_exc_t wr_setpoint(uint16_t reg, uint16_t v) {
    (void)reg;
    if ((v < 5U) || (v > 30U)) { return MB_EXC_ILLEGAL_VALUE; }
    heater_set_setpoint_c((int8_t)v);
    return MB_EXC_NONE;
}
static modbus_exc_t rd_type_select(uint16_t reg, uint16_t *o) { (void)reg; *o = heater_get_type_select(); return MB_EXC_NONE; }
static modbus_exc_t wr_type_select(uint16_t reg, uint16_t v) {
    (void)reg;
    if (v > 0xFFU) { return MB_EXC_ILLEGAL_VALUE; }   /* coprocessor validates the id */
    heater_set_type_select((uint8_t)v);
    return MB_EXC_NONE;
}

static modbus_exc_t rd_command(uint16_t reg, uint16_t *o) { heater_telemetry_t t = tel(); (void)reg; *o = t.cmd_result; return MB_EXC_NONE; }
static modbus_exc_t wr_command(uint16_t reg, uint16_t v) {
    (void)reg;
    if ((v > 0xFFU) || !heater_send_command((uint8_t)v)) { return MB_EXC_ILLEGAL_VALUE; }
    return MB_EXC_NONE;
}

static modbus_exc_t rd_poll_log(uint16_t reg, uint16_t *value)
{
    *value = heater_poll_diagnostic(reg);
    return MB_EXC_NONE;
}

/* Read-only telemetry: one tiny reader per register. */
#define RO(name, expr) \
    static modbus_exc_t name(uint16_t reg, uint16_t *o) { heater_telemetry_t t = tel(); (void)reg; *o = (uint16_t)(expr); return MB_EXC_NONE; }
RO(rd_state, t.state)
RO(rd_active_level, t.active_level)
RO(rd_error, t.error)
RO(rd_supply_mv, t.supply_mv)
RO(rd_fan_rpm, t.fan_rpm)
RO(rd_pump_hz_x10, t.pump_hz_x10)
RO(rd_exchanger_raw, t.exchanger_raw)
RO(rd_state_seconds, t.state_seconds)
RO(rd_age_ms, t.age_ms)
RO(rd_flags, t.flags)
RO(rd_valid_frames, t.valid_frames)
RO(rd_checksum_failures, t.checksum_failures)
RO(rd_transport_errors, t.transport_errors)
RO(rd_type, t.type)
RO(rd_caps, t.caps)
RO(rd_phase, t.phase)
RO(rd_vendor_state, t.vendor_state)
RO(rd_detect, t.detect)

void mbp_heater_register(void) {
    mb_reg_bind(53, rd_request,           wr_request);
    mb_reg_bind(54, rd_level,             wr_level);
    mb_reg_bind(55, rd_state,             0);
    mb_reg_bind(56, rd_active_level,      0);
    mb_reg_bind(57, rd_error,             0);
    mb_reg_bind(58, rd_supply_mv,         0);
    mb_reg_bind(59, rd_fan_rpm,           0);
    mb_reg_bind(60, rd_pump_hz_x10,       0);
    mb_reg_bind(61, rd_exchanger_raw,     0);
    mb_reg_bind(62, rd_state_seconds,     0);
    mb_reg_bind(63, rd_age_ms,            0);
    for (uint16_t reg = HEATER_POLL_LOG_BASE; reg <= HEATER_POLL_MAX_GAP_HI; ++reg)
        mb_reg_bind(reg, rd_poll_log, 0);
    mb_reg_bind(64, rd_flags,             0);
    mb_reg_bind(65, rd_valid_frames,      0);
    mb_reg_bind(66, rd_checksum_failures, 0);
    mb_reg_bind(67, rd_transport_errors,  0);
    mb_reg_bind(68, rd_type,              0);
    mb_reg_bind(69, rd_setpoint,          wr_setpoint);
    mb_reg_bind(70, rd_caps,              0);
    mb_reg_bind(71, rd_phase,             0);
    mb_reg_bind(72, rd_vendor_state,      0);
    mb_reg_bind(73, rd_detect,            0);
    mb_reg_bind(74, rd_type_select,       wr_type_select);
    mb_reg_bind(75, rd_command,           wr_command);
}
