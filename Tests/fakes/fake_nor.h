#ifndef FAKE_NOR_H
#define FAKE_NOR_H
#include "nvm_backend.h"
#define FAKE_NOR_SECTOR_SIZE  4096u
#define FAKE_NOR_SECTOR_COUNT 4u
void fake_nor_init(nvm_backend_t *be);   /* wire be to the fake; start fully erased */
void fake_nor_reset(void);               /* erase all to 0xFF */
void fake_nor_fail_writes(int on);       /* 1 = program/erase return -1 (backend failure injection) */
uint8_t *fake_nor_raw(void);             /* backing array (for corruption tests) */
uint32_t fake_nor_size(void);            /* total bytes */
#endif
