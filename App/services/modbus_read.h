#ifndef MODBUS_READ_H
#define MODBUS_READ_H
#include <stdint.h>
#include <stdbool.h>
typedef bool (*mb_reg_read_fn)(uint16_t reg, uint16_t *out);
/* Build an FC-0x03 response; resp must hold at least 5 + 2*count bytes (max 255 for valid count 1..125). */
uint16_t mb_build_read_holding(uint8_t addr, uint16_t start, uint16_t count,
                               mb_reg_read_fn reader, uint8_t *resp);
#endif
