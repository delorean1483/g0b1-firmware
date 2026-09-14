#ifndef FAKE_IFLASH_H
#define FAKE_IFLASH_H
#include "iflash_backend.h"
const iflash_backend_t *fake_iflash_backend(void);
void     fake_iflash_reset(void);
uint8_t *fake_iflash_ptr(uint32_t addr);
void     fake_iflash_fail_next_program(void);
void     fake_iflash_fail_next_erase(void);
#endif
