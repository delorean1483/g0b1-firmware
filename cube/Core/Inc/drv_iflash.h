#ifndef DRV_IFLASH_H
#define DRV_IFLASH_H

/* Concrete STM32G0 internal-flash backend for the portable iflash service
 * (App/services). Task 7 of the bootloader/A-B update plan.
 *
 * program()/erase_page() inner loops execute from RAM (placed in the
 * .RamFunc linker section — see STM32G0B1RETX_FLASH.ld) because the G0B1
 * is single-bank: code fetches from FLASH stall while a program/erase
 * operation is in progress, so the code driving that operation cannot
 * itself live in FLASH.
 */
#include "iflash_backend.h"

const iflash_backend_t *drv_iflash_backend(void);

#endif
