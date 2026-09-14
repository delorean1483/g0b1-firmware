#ifndef BL_PROTO_H
#define BL_PROTO_H
#include <stdint.h>

/* FC 0x41/0x42 bootloader firmware-transfer wire format.
 * FROZEN: sub-project #2 (cortex agent) implements the host side of this
 * exact byte layout. See task-5 brief "Wire framing (freeze this)".
 *
 * 0x41 CONTROL (sub in first data byte):
 *   INFO   req [1][0x41][0x01]                                  resp [1][0x41][0x01][ver][slot][size:4 BE][chunk:2 BE][algo]
 *   ERASE  req [1][0x41][0x02]                                  resp ACK [1][0x41][0x02][0x00]   NAK [1][0xC1][err]
 *   VERIFY req [1][0x41][0x03][length:4 BE][crc32:4 BE]         resp ACK/NAK as ERASE
 *   COMMIT req [1][0x41][0x04]                                  resp ACK, then bootloader drains TX + NVIC_SystemReset
 *   ABORT  req [1][0x41][0x05]                                  resp ACK
 *   STATUS req [1][0x41][0x06]                                  resp [1][0x41][0x06][state:1][high_water:4 BE]
 * (trailing [crc16] on every frame, added/checked by the caller, not this codec)
 *
 * 0x42 DATA:
 *   req  [1][0x42][offset:4 BE][len:1][data:len]
 *   resp ACK [1][0x42][offset:4 BE]   NAK [1][0xC2][err]
 *
 * All bl_build_* functions write the response BODY only (no CRC16); the
 * caller appends modbus_crc16 exactly like mb_engine's finalize convention.
 * All bl_parse_* functions operate on the PDU WITHOUT the trailing CRC16
 * (the caller already validated it via mb_check_frame).
 */

#define BL_FC_CONTROL 0x41u
#define BL_FC_DATA    0x42u
#define BL_CHUNK_MAX  240u   /* data bytes per 0x42 chunk (multiple of 8) */

typedef enum {
    BL_SUB_INFO   = 0x01u,
    BL_SUB_ERASE  = 0x02u,
    BL_SUB_VERIFY = 0x03u,
    BL_SUB_COMMIT = 0x04u,
    BL_SUB_ABORT  = 0x05u,
    BL_SUB_STATUS = 0x06u
} bl_sub_t;

#define BL_ERR_STATE 1u
#define BL_ERR_RANGE 2u
#define BL_ERR_CRC   3u
#define BL_ERR_FLASH 4u

typedef struct {
    uint8_t  bl_version;
    uint8_t  inactive_slot;
    uint32_t slot_size;
    uint16_t chunk_max;
    uint8_t  crc_algo;      /* 1 = CRC32/IEEE */
} bl_info_t;

int bl_parse_control(const uint8_t *req, uint16_t len, bl_sub_t *sub, uint32_t *a, uint32_t *b);
int bl_parse_data(const uint8_t *req, uint16_t len, uint32_t *off, const uint8_t **data, uint16_t *dlen);

uint16_t bl_build_info(uint8_t *resp, const bl_info_t *i);
uint16_t bl_build_ack(uint8_t *resp, uint8_t fc, bl_sub_t sub);
uint16_t bl_build_status(uint8_t *resp, uint8_t state, uint32_t high_water);
uint16_t bl_build_data_ack(uint8_t *resp, uint32_t off);
uint16_t bl_build_nak(uint8_t *resp, uint8_t fc, uint8_t err);
#endif
