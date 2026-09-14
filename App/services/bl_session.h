#ifndef BL_SESSION_H
#define BL_SESSION_H
#include <stdint.h>
#include "iflash_backend.h"
#include "bootcfg.h"

typedef enum { BL_ST_IDLE, BL_ST_ERASED, BL_ST_VERIFIED, BL_ST_COMMITTING, BL_ST_ERROR } bl_state_t;

typedef struct {
    const iflash_backend_t *fl;
    bootcfg_t *cfg;
    slot_id_t  target;
    uint32_t   target_base;
    uint32_t   slot_size;
    uint32_t   expected_len;
    uint32_t   expected_crc;
    uint32_t   high_water;
    bl_state_t state;
    int        want_reset;
} bl_session_t;

void bl_session_init(bl_session_t *s, const iflash_backend_t *fl, bootcfg_t *cfg);
uint16_t bl_session_process(bl_session_t *s, const uint8_t *req, uint16_t req_len, uint8_t *resp);
#endif
