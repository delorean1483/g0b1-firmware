#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    return crc32_update(CRC32_INIT, data, len) ^ 0xFFFFFFFFu;
}
