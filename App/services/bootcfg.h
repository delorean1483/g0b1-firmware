#ifndef BOOTCFG_H
#define BOOTCFG_H
#include <stdint.h>
#include "iflash_backend.h"

#define BOOTCFG_MAGIC    0x42434647u  /* 'GFCB' little-endian == "BCFG" */
#define BOOTCFG_VERSION  1u
#define BOOTCFG_PAGE_0   240u
#define BOOTCFG_PAGE_1   241u

#define SLOT_A_BASE      0x08008000u
#define SLOT_B_BASE      0x08040000u
#define SLOT_SIZE        0x38000u      /* 224 KB */

typedef enum { SLOT_STATE_BAD = 0, SLOT_STATE_TRIAL = 1, SLOT_STATE_COMMITTED = 2 } slot_state_t;
typedef enum { SLOT_A = 0, SLOT_B = 1 } slot_id_t;

typedef struct { uint8_t state; uint8_t _pad[3]; uint32_t length; uint32_t crc32; } slot_meta_t;

typedef struct {
    uint32_t    magic;
    uint16_t    version;
    uint16_t    active_slot;
    uint32_t    seq;
    slot_meta_t slot[2];
    uint16_t    trial_count;
    uint16_t    _pad;
    uint32_t    _pad2;
    uint32_t    crc32;        /* over all preceding bytes */
} bootcfg_t;

uint32_t bootcfg_slot_base(slot_id_t s);
int bootcfg_load(const iflash_backend_t *fl, bootcfg_t *out);
int bootcfg_save(const iflash_backend_t *fl, bootcfg_t *cfg);
int bootcfg_slot_crc_ok(const iflash_backend_t *fl, const bootcfg_t *cfg, slot_id_t s);
#endif
