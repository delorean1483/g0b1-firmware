#ifndef L011_LOADER_CORE_H
#define L011_LOADER_CORE_H
#include <stdbool.h>
#include <stdint.h>
bool loader_valid(void);
bool loader_command(const uint8_t *frame, unsigned size);
bool loader_erase(uint32_t address);
bool loader_word(uint32_t address, uint32_t value);
uint32_t loader_read(uint32_t address);
#endif
