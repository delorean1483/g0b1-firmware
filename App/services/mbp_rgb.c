#include "mbp_rgb.h"
#include "mb_regmodel.h"
#include "rgb.h"
#include "is31fl3293.h"

static modbus_exc_t rd_chan(uint16_t reg, uint16_t *o)
{
    uint8_t a[3];
    rgb_get_applied(a);
    *o = a[reg - 76U];
    return MB_EXC_NONE;
}

static modbus_exc_t wr_chan(uint16_t reg, uint16_t v)
{
    uint8_t q[3];
    if (v > 255U) { return MB_EXC_ILLEGAL_VALUE; }
    rgb_get_requested(q);
    q[reg - 76U] = (uint8_t)v;
    rgb_set_logical(q[0], q[1], q[2]);
    return MB_EXC_NONE;
}

static modbus_exc_t rd_status(uint16_t reg, uint16_t *o)
{
    (void)reg;
    *o = rgb_get_status();
    return MB_EXC_NONE;
}

static modbus_exc_t rd_diagnostic(uint16_t reg, uint16_t *o)
{
    *o = rgb_get_diagnostic(reg);
    return MB_EXC_NONE;
}

#ifdef RGB_BENCH_INJECTION
static modbus_exc_t wr_bench_injection(uint16_t reg, uint16_t mode)
{
    (void)reg;
    uint16_t heater_mode, heater_run;
    if (mode && (mb_reg_read(10U, &heater_mode) != MB_EXC_NONE || heater_mode != 0u ||
                 mb_reg_read(53U, &heater_run) != MB_EXC_NONE || heater_run != 0u))
        return MB_EXC_ILLEGAL_VALUE;
    return rgb_bench_arm(mode) == 0 ? MB_EXC_NONE : MB_EXC_ILLEGAL_VALUE;
}
#endif

static modbus_exc_t rd_pwm_hz(uint16_t reg, uint16_t *value)
{
    (void)reg;
    *value = is31_pwm_hz();
    return MB_EXC_NONE;
}

static modbus_exc_t wr_pwm_hz(uint16_t reg, uint16_t value)
{
    (void)reg;
    if (value != 200u && value != 400u) return MB_EXC_ILLEGAL_VALUE;
    uint8_t applied[3], requested[3];
    rgb_get_applied(applied);
    rgb_get_requested(requested);
    if ((applied[0] | applied[1] | applied[2] | requested[0] | requested[1] | requested[2]) != 0u ||
        !(rgb_get_status() & RGB_ST_APPLY_OK))
        return MB_EXC_ILLEGAL_VALUE;
    is31_set_pwm_hz(value);
    return MB_EXC_NONE;
}

void mbp_rgb_register(void)
{
    mb_reg_bind(76U, rd_chan, wr_chan);
    mb_reg_bind(77U, rd_chan, wr_chan);
    mb_reg_bind(78U, rd_chan, wr_chan);
    mb_reg_bind(79U, rd_status, 0);
    mb_reg_bind(155U, rd_pwm_hz, wr_pwm_hz);
#ifdef RGB_BENCH_INJECTION
    mb_reg_bind(154U, rd_diagnostic, wr_bench_injection);
#endif
    for (uint16_t reg = 139; reg <= 153; ++reg)
        mb_reg_bind(reg, rd_diagnostic, 0);
    for (uint16_t reg = 1504; reg <= 1887; ++reg)
        mb_reg_bind(reg, rd_diagnostic, 0);
    for (uint16_t reg = RGB_RETRY_LOG_BASE; reg <= RGB_RETRY_LOG_END; ++reg)
        mb_reg_bind(reg, rd_diagnostic, 0);
    for (uint16_t reg = 84; reg <= 91; ++reg)
        mb_reg_bind(reg, rd_diagnostic, 0);
}
