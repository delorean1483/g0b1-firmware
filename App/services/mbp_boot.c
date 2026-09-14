#include "mbp_boot.h"
#include "mb_regmodel.h"
#include "modbus_defs.h"

static int  (*s_engine_running)(void);
static void (*s_enter_bl)(void);

static modbus_exc_t rd_boot(uint16_t reg, uint16_t *o) { (void)reg; *o = 0; return MB_EXC_NONE; }

static modbus_exc_t wr_boot(uint16_t reg, uint16_t v) {
    (void)reg;
    if (v != MBP_BOOT_ARM_VALUE) return MB_EXC_NONE;                              /* ignore stray writes */
    if (s_engine_running && s_engine_running()) return MB_EXC_SLAVE_DEVICE_FAILURE; /* refuse */
    if (s_enter_bl) s_enter_bl();                                                 /* arms magic + resets */
    return MB_EXC_NONE;
}

void mbp_boot_register(int (*engine_running)(void), void (*enter_bl)(void)) {
    s_engine_running = engine_running;
    s_enter_bl = enter_bl;
    mb_reg_bind(35, rd_boot, wr_boot);
}
