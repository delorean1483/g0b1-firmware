#ifndef MODBUS_FRAME_H
#define MODBUS_FRAME_H
#include <stdint.h>
typedef enum { MB_FRAME_OK, MB_FRAME_TOO_SHORT, MB_FRAME_NOT_FOR_US, MB_FRAME_BAD_CRC } mb_frame_status_t;
/* Validate an RTU frame: length >=4, address match/broadcast, trailing CRC (lo,hi). */
mb_frame_status_t mb_check_frame(const uint8_t *buf, uint16_t len, uint8_t our_addr);
#endif
