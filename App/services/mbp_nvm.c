#include "mbp_nvm.h"
#include "mb_regmodel.h"
#include "nvm.h"
#include "nvm_map.h"

/* reg -> (address, is_byte, max valid value or 0xFFFF for none). */
typedef struct { uint16_t reg; uint16_t addr; uint8_t is_byte; uint16_t vmax; } nvm_map_row_t;
static const nvm_map_row_t s_rows[] = {
    { 11, ENGINE_RUNTIME_START,    0, 0xFFFF },
    { 20, ENGINE_OILTIME_START,    0, 0xFFFF },
    { 21, MACHINE_RUNTIME_START,   0, 0xFFFF },
    { 12, EE_EVAP_FAN_SPEED,       1, 2 },
    { 13, EE_MONITOR_BATT_SETTING, 0, 0xFFFF },
    { 14, EE_CLIMATE_TEMP_SETTING, 0, 0xFFFF },
    { 15, EE_STORAGE_TEMP_SETTING, 0, 0xFFFF },
    { 16, EE_STORAGE_BATT_SETTING, 0, 0xFFFF },
    { 19, EE_TEMP_UNIT,            1, 1 },
    { 36, EE_VREF_CALIBRATION,     0, 0xFFFF },
    { 37, EE_TEMP_CALIBRATION,     0, 0xFFFF },
};
#define NVM_ROW_COUNT (sizeof s_rows / sizeof s_rows[0])

static const nvm_map_row_t *row_for(uint16_t reg) {
    for (unsigned i = 0; i < NVM_ROW_COUNT; i++) if (s_rows[i].reg == reg) return &s_rows[i];
    return 0;
}

static modbus_exc_t rd_nvm(uint16_t reg, uint16_t *o) {
    const nvm_map_row_t *r = row_for(reg);
    if (!r) return MB_EXC_ILLEGAL_ADDRESS;
    *o = r->is_byte ? nvm_read_byte(r->addr) : nvm_read_word(r->addr);
    return MB_EXC_NONE;
}
static modbus_exc_t wr_nvm(uint16_t reg, uint16_t v) {
    const nvm_map_row_t *r = row_for(reg);
    if (!r) return MB_EXC_ILLEGAL_ADDRESS;
    if (r->vmax != 0xFFFF && v > r->vmax) return MB_EXC_ILLEGAL_VALUE;
    if (r->is_byte) nvm_write_byte(r->addr, (uint8_t)v);
    else            nvm_write_word(r->addr, v);
    nvm_commit();
    return MB_EXC_NONE;
}

void mbp_nvm_register(void) {
    for (unsigned i = 0; i < NVM_ROW_COUNT; i++) mb_reg_bind(s_rows[i].reg, rd_nvm, wr_nvm);
}
