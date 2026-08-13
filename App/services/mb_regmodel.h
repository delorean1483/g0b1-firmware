#ifndef MB_REGMODEL_H
#define MB_REGMODEL_H
#include "modbus_defs.h"

/* A provider read returns MB_EXC_NONE and sets *out, or an exception code.
   A provider write returns MB_EXC_NONE, or ILLEGAL_VALUE for out-of-range. */
typedef modbus_exc_t (*mb_reg_read_fn)(uint16_t reg, uint16_t *out);
typedef modbus_exc_t (*mb_reg_write_fn)(uint16_t reg, uint16_t val);

void         mb_reg_reset(void);   /* drop all bindings */
void         mb_reg_bind(uint16_t reg, mb_reg_read_fn rd, mb_reg_write_fn wr); /* wr NULL = read-only */
modbus_exc_t mb_reg_read(uint16_t reg, uint16_t *out);  /* unbound/no reader -> ILLEGAL_ADDRESS */
modbus_exc_t mb_reg_write(uint16_t reg, uint16_t val);  /* unbound/no writer -> ILLEGAL_ADDRESS */

#endif /* MB_REGMODEL_H */
