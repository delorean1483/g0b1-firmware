#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H
#include <stdint.h>
#include "g0b1_slots.h"
extern volatile uint32_t g_boot_magic __attribute__((section(".noinit")));
void boot_jump_to_slot(uint32_t slot_base); /* no return */
#endif
