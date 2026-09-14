#ifndef CRC32_H
#define CRC32_H
#include <stdint.h>
/* CRC-32/IEEE-802.3 (zlib crc32): reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF.
   This is the interop contract with the cortex agent's per-.bin CRC. */
#define CRC32_INIT 0xFFFFFFFFu
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len); /* NO xorout; chainable */
uint32_t crc32_compute(const uint8_t *data, uint32_t len);              /* seeds + xorout */
#endif
