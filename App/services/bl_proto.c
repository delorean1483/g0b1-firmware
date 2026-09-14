#include "bl_proto.h"

#define ADDR 1u

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int bl_parse_control(const uint8_t *req, uint16_t len, bl_sub_t *sub, uint32_t *a, uint32_t *b) {
    if (len < 3u || req[1] != BL_FC_CONTROL) return -1;
    *sub = (bl_sub_t)req[2]; *a = 0; *b = 0;
    if (*sub == BL_SUB_VERIFY) {
        if (len < 3u + 8u) return -1;
        *a = rd32(&req[3]); *b = rd32(&req[7]);
    }
    return 0;
}

int bl_parse_data(const uint8_t *req, uint16_t len, uint32_t *off, const uint8_t **data, uint16_t *dlen) {
    if (len < 7u || req[1] != BL_FC_DATA) return -1;
    *off = rd32(&req[2]);
    uint16_t n = req[6];
    if (n == 0u || n > BL_CHUNK_MAX) return -1;
    if ((uint32_t)7u + n > len) return -1;           /* claimed len must fit */
    *data = &req[7]; *dlen = n;
    return 0;
}

uint16_t bl_build_info(uint8_t *resp, const bl_info_t *i) {
    /* Body: [addr][fc][sub][ver][slot][size:4 BE][chunk:2 BE][algo] = 12 bytes. */
    resp[0] = ADDR; resp[1] = BL_FC_CONTROL; resp[2] = BL_SUB_INFO;
    resp[3] = i->bl_version; resp[4] = i->inactive_slot;
    be32(&resp[5], i->slot_size);
    resp[9]  = (uint8_t)(i->chunk_max >> 8);
    resp[10] = (uint8_t)i->chunk_max;
    resp[11] = i->crc_algo;
    return 12u;
}

uint16_t bl_build_ack(uint8_t *resp, uint8_t fc, bl_sub_t sub) {
    resp[0] = ADDR; resp[1] = fc; resp[2] = (uint8_t)sub; resp[3] = 0x00u;
    return 4u;
}

uint16_t bl_build_status(uint8_t *resp, uint8_t state, uint32_t high_water) {
    resp[0] = ADDR; resp[1] = BL_FC_CONTROL; resp[2] = BL_SUB_STATUS; resp[3] = state;
    be32(&resp[4], high_water);
    return 8u;
}

uint16_t bl_build_data_ack(uint8_t *resp, uint32_t off) {
    resp[0] = ADDR; resp[1] = BL_FC_DATA;
    be32(&resp[2], off);
    return 6u;
}

uint16_t bl_build_nak(uint8_t *resp, uint8_t fc, uint8_t err) {
    resp[0] = ADDR; resp[1] = (uint8_t)(fc | 0x80u); resp[2] = err;
    return 3u;
}
