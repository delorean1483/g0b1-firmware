#include "modbus_frame.h"
#include "modbus_crc.h"

mb_frame_status_t mb_check_frame(const uint8_t *buf, uint16_t len, uint8_t our_addr)
{
    if (len < 4u) return MB_FRAME_TOO_SHORT;
    if (buf[0] != our_addr && buf[0] != 0x00u) return MB_FRAME_NOT_FOR_US;
    uint16_t calc = modbus_crc16(buf, (uint16_t)(len - 2u));
    uint16_t recv = (uint16_t)(buf[len - 2u] | ((uint16_t)buf[len - 1u] << 8));
    if (calc != recv) return MB_FRAME_BAD_CRC;
    return MB_FRAME_OK;
}
