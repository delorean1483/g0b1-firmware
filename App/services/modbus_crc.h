#ifndef MODBUS_CRC_H
#define MODBUS_CRC_H
#include <stdint.h>
/* Modbus RTU CRC-16: poly 0xA001 (reflected), init 0xFFFF, xorout 0x0000. */
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);
#endif
