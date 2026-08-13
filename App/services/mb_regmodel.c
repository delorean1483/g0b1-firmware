#include "mb_regmodel.h"

typedef struct { mb_reg_read_fn rd; mb_reg_write_fn wr; } mb_reg_slot_t;
static mb_reg_slot_t s_slots[MB_REG_MAX + 1u];   /* index by register number 1..52 */

void mb_reg_reset(void) {
    for (uint16_t r = 0; r <= MB_REG_MAX; r++) { s_slots[r].rd = 0; s_slots[r].wr = 0; }
}

void mb_reg_bind(uint16_t reg, mb_reg_read_fn rd, mb_reg_write_fn wr) {
    if (reg >= 1u && reg <= MB_REG_MAX) { s_slots[reg].rd = rd; s_slots[reg].wr = wr; }
}

modbus_exc_t mb_reg_read(uint16_t reg, uint16_t *out) {
    if (reg < 1u || reg > MB_REG_MAX || s_slots[reg].rd == 0) return MB_EXC_ILLEGAL_ADDRESS;
    return s_slots[reg].rd(reg, out);
}

modbus_exc_t mb_reg_write(uint16_t reg, uint16_t val) {
    if (reg < 1u || reg > MB_REG_MAX || s_slots[reg].wr == 0) return MB_EXC_ILLEGAL_ADDRESS;
    return s_slots[reg].wr(reg, val);
}
