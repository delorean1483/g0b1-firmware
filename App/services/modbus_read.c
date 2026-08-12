#include "modbus_read.h"
#include "modbus_crc.h"

static uint16_t append_crc(uint8_t *resp, uint16_t len) {
    uint16_t c = modbus_crc16(resp, len);
    resp[len]     = (uint8_t)(c & 0xFF);
    resp[len + 1] = (uint8_t)(c >> 8);
    return (uint16_t)(len + 2u);
}
static uint16_t build_exception(uint8_t *resp, uint8_t addr, uint8_t func, uint8_t code) {
    resp[0] = addr;
    resp[1] = (uint8_t)(func | 0x80u);
    resp[2] = code;
    return append_crc(resp, 3u);
}

uint16_t mb_build_read_holding(uint8_t addr, uint16_t start, uint16_t count,
                               mb_reg_read_fn reader, uint8_t *resp)
{
    /* Validate register count: Modbus FC-0x03 allows 1..125 registers */
    if (count < 1u || count > 125u) {
        return build_exception(resp, addr, 0x03u, 0x03u); /* illegal data value */
    }

    for (uint16_t i = 0; i < count; i++) {
        uint16_t v;
        if (!reader((uint16_t)(start + i), &v)) {
            return build_exception(resp, addr, 0x03u, 0x02u); /* illegal data address */
        }
        /* Write big-endian register value directly into response */
        resp[3 + 2u * i]     = (uint8_t)(v >> 8);   /* hi */
        resp[3 + 2u * i + 1u] = (uint8_t)(v & 0xFF); /* lo */
    }
    resp[0] = addr;
    resp[1] = 0x03u;
    resp[2] = (uint8_t)(2u * count);
    return append_crc(resp, (uint16_t)(3u + 2u * count));
}
