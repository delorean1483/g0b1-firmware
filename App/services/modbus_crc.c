#include "modbus_crc.h"
uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001u) { crc = (uint16_t)((crc >> 1) ^ 0xA001u); }
            else               { crc = (uint16_t)(crc >> 1); }
        }
    }
    return crc;
}
