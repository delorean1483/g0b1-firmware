#include "mbp_rgb.h"
#include "mb_regmodel.h"
#include "rgb.h"

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

void mbp_rgb_register(void)
{
    mb_reg_bind(76U, rd_chan, wr_chan);
    mb_reg_bind(77U, rd_chan, wr_chan);
    mb_reg_bind(78U, rd_chan, wr_chan);
    mb_reg_bind(79U, rd_status, 0);
}
