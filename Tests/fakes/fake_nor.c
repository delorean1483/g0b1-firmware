#include "fake_nor.h"
#include <string.h>

#define NOR_SIZE (FAKE_NOR_SECTOR_SIZE * FAKE_NOR_SECTOR_COUNT)
static uint8_t s_mem[NOR_SIZE];
static int s_fail_writes;   /* when set, program/erase report a backend failure */

static int f_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len) {
    (void)ctx;
    if ((uint64_t)addr + len > NOR_SIZE) return -1;
    memcpy(buf, &s_mem[addr], len);
    return 0;
}
static int f_program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len) {
    (void)ctx;
    if (s_fail_writes) return -1;
    if ((uint64_t)addr + len > NOR_SIZE) return -1;
    for (uint32_t i = 0; i < len; i++) s_mem[addr + i] &= buf[i];  /* AND: bits only clear */
    return 0;
}
static int f_erase(void *ctx, uint32_t sector) {
    (void)ctx;
    if (s_fail_writes) return -1;
    if (sector >= FAKE_NOR_SECTOR_COUNT) return -1;
    memset(&s_mem[sector * FAKE_NOR_SECTOR_SIZE], 0xFF, FAKE_NOR_SECTOR_SIZE);
    return 0;
}
void fake_nor_reset(void) { memset(s_mem, 0xFF, NOR_SIZE); s_fail_writes = 0; }
void fake_nor_fail_writes(int on) { s_fail_writes = on; }
void fake_nor_init(nvm_backend_t *be) {
    fake_nor_reset();
    be->sector_size  = FAKE_NOR_SECTOR_SIZE;
    be->sector_count = FAKE_NOR_SECTOR_COUNT;
    be->read = f_read; be->program = f_program; be->erase = f_erase; be->ctx = 0;
}
uint8_t *fake_nor_raw(void) { return s_mem; }
uint32_t fake_nor_size(void) { return NOR_SIZE; }
