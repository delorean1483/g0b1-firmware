#ifndef DRV_IFLASH_H
#define DRV_IFLASH_H

/* Concrete STM32G0 internal-flash backend for the portable iflash service
 * (App/services). Task 7 of the bootloader/A-B update plan.
 *
 * Standard HAL flash: program()/erase_page() run from FLASH using the plain
 * STM32 HAL. The G0B1 is single-bank, so a program/erase operation
 * transparently stalls the CPU's instruction fetch for its duration — a
 * safe hardware bus pause, not a hazard (ST's own G0 HAL does not mark
 * HAL_FLASH_Program/HAL_FLASHEx_Erase as __RAM_FUNC). IWDG is refreshed
 * before each page erase.
 */
#include "iflash_backend.h"

const iflash_backend_t *drv_iflash_backend(void);

#endif
