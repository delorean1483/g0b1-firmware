#ifndef NVM_H
#define NVM_H
#include <stdint.h>
#include <stdbool.h>
#include "nvm_backend.h"
void     nvm_init(const nvm_backend_t *be);
uint8_t  nvm_read_byte(uint16_t addr);
uint16_t nvm_read_word(uint16_t addr);
void     nvm_write_byte(uint16_t addr, uint8_t v);
void     nvm_write_word(uint16_t addr, uint16_t v);
bool     nvm_dirty(void);
int      nvm_commit(void);
#endif
