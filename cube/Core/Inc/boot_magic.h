#ifndef BOOT_MAGIC_H
#define BOOT_MAGIC_H
#include <stdint.h>

/* Shared reset-magic word, app side. Lives in the .noinit linker section
 * placed at 0x20023FE0 by every slot linker script (STM32G0B1RETX_SLOTA.ld /
 * STM32G0B1RETX_SLOTB.ld) — the SAME physical address the bootloader's own
 * g_boot_magic (boot/src/boot_jump.h, boot/g0b1-boot.ld) uses. Writing
 * BOOT_MAGIC_ENTER (boot_decision.h) here just before NVIC_SystemReset()
 * tells the bootloader, on the next boot, to enter update mode instead of
 * jumping straight to the active app slot. Defined once, in app_main.c. */
extern volatile uint32_t g_boot_magic __attribute__((section(".noinit")));

#endif /* BOOT_MAGIC_H */
