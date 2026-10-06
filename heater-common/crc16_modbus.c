#include "crc16_modbus.h"

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    size_t i;
    uint8_t b;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (b = 0u; b < 8u; ++b) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
