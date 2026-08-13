#include "mbp_sensors.h"
#include "mb_regmodel.h"
#include "sensors.h"

static const rpm_source_t *s_rpm_src;

static modbus_exc_t rd_encl(uint16_t reg, uint16_t *o) { (void)reg; *o = (uint16_t)sensors_get_encl_temp_f(); return MB_EXC_NONE; }
static modbus_exc_t rd_extadc(uint16_t reg, uint16_t *o) { (void)reg; *o = sensors_get_ext_adc(); return MB_EXC_NONE; }
static modbus_exc_t rd_batt(uint16_t reg, uint16_t *o) { (void)reg; *o = sensors_get_batt_cv(); return MB_EXC_NONE; }
static modbus_exc_t rd_rpm(uint16_t reg, uint16_t *o) { (void)reg; *o = rpm_read(s_rpm_src); return MB_EXC_NONE; }
static modbus_exc_t rd_extf(uint16_t reg, uint16_t *o) { (void)reg; *o = (uint16_t)sensors_get_ext_temp_f(); return MB_EXC_NONE; }

void mbp_sensors_register(const rpm_source_t *rpm_src) {
    s_rpm_src = rpm_src;
    mb_reg_bind(1,  rd_encl,   0);
    mb_reg_bind(3,  rd_extadc, 0);
    mb_reg_bind(6,  rd_batt,   0);
    mb_reg_bind(38, rd_rpm,    0);
    mb_reg_bind(51, rd_extf,   0);
}
