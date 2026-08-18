#ifndef DRV_S25FL064_H
#define DRV_S25FL064_H

/* Concrete SPI2 NOR backend for the portable nvm service (Task 6).
 *
 * S25FL064 (64 Mbit / 8 MByte, 4 KB sectors, 256 B program pages) on SPI2
 * (PD1 SCK / PD3 MISO / PD4 MOSI) with a software chip-select on PC2. Implements
 * nvm_backend_t; program() splits every write at 256-byte page boundaries via
 * the host-tested nor_page_split helper.
 *
 * Bench bring-up plan Task 6:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "nvm_backend.h"

/* Factory: hand this to nvm_init(). */
const nvm_backend_t *drv_s25fl064_backend(void);

/* Bench aid: read JEDEC ID (0x9F) -> id[0]=manufacturer, [1]=type, [2]=capacity.
 * Returns 0 on success. Use to confirm the part is alive/correct on the bench. */
int drv_s25fl064_read_id(uint8_t id[3]);

#endif /* DRV_S25FL064_H */
