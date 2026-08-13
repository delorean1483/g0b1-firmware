#include "mbp_sys.h"
#include "mb_regmodel.h"

static uint16_t    s_dspl_fw;
static uint16_t    s_boot_flag;
static mb_reset_fn s_on_reset;

static modbus_exc_t rd_relay_fw(uint16_t reg, uint16_t *o) { (void)reg; *o = MB_RELAY_FW_VERSION; return MB_EXC_NONE; }
static modbus_exc_t rd_dspl_fw(uint16_t reg, uint16_t *o) { (void)reg; *o = s_dspl_fw; return MB_EXC_NONE; }
static modbus_exc_t wr_dspl_fw(uint16_t reg, uint16_t v) { (void)reg; s_dspl_fw = v; return MB_EXC_NONE; }
static modbus_exc_t rd_reset(uint16_t reg, uint16_t *o) { (void)reg; *o = 0; return MB_EXC_NONE; }
static modbus_exc_t wr_reset(uint16_t reg, uint16_t v) { (void)reg; (void)v; if (s_on_reset) s_on_reset(); return MB_EXC_NONE; }
static modbus_exc_t rd_boot(uint16_t reg, uint16_t *o) { (void)reg; *o = s_boot_flag; return MB_EXC_NONE; }
static modbus_exc_t wr_boot(uint16_t reg, uint16_t v) { (void)reg; s_boot_flag = v; return MB_EXC_NONE; }

void mbp_sys_register(mb_reset_fn on_reset) {
    s_on_reset = on_reset;
    s_dspl_fw = 0;
    s_boot_flag = 0;
    mb_reg_bind(34, rd_reset,    wr_reset);
    mb_reg_bind(35, rd_boot,     wr_boot);
    mb_reg_bind(39, rd_relay_fw, 0);        /* read-only constant */
    mb_reg_bind(40, rd_dspl_fw,  wr_dspl_fw);
}
