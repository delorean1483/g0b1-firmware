#include "mbp_heater.h"
#include "mb_regmodel.h"
#include "heater.h"

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

static modbus_exc_t rd_state(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.state; return MB_EXC_NONE;
}
static modbus_exc_t rd_active_level(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.active_level; return MB_EXC_NONE;
}
static modbus_exc_t rd_error(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.error; return MB_EXC_NONE;
}
static modbus_exc_t rd_supply_mv(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.supply_mv; return MB_EXC_NONE;
}
static modbus_exc_t rd_fan_rpm(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.fan_rpm; return MB_EXC_NONE;
}
static modbus_exc_t rd_pump_hz_x10(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.pump_hz_x10; return MB_EXC_NONE;
}
static modbus_exc_t rd_exchanger_raw(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.exchanger_raw; return MB_EXC_NONE;
}
static modbus_exc_t rd_state_seconds(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.state_seconds; return MB_EXC_NONE;
}
static modbus_exc_t rd_age_ms(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.age_ms; return MB_EXC_NONE;
}
static modbus_exc_t rd_flags(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.flags; return MB_EXC_NONE;
}
static modbus_exc_t rd_valid_frames(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.valid_frames; return MB_EXC_NONE;
}
static modbus_exc_t rd_checksum_failures(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.checksum_failures; return MB_EXC_NONE;
}
static modbus_exc_t rd_transport_errors(uint16_t reg, uint16_t *o) {
    (void)reg; heater_telemetry_t t; heater_get_telemetry(&t); *o = t.transport_errors; return MB_EXC_NONE;
}

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
    mb_reg_bind(64, rd_flags,             0);
    mb_reg_bind(65, rd_valid_frames,      0);
    mb_reg_bind(66, rd_checksum_failures, 0);
    mb_reg_bind(67, rd_transport_errors,  0);
}
