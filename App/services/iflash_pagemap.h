#ifndef IFLASH_PAGEMAP_H
#define IFLASH_PAGEMAP_H
#include <stdint.h>

/* Map a flat internal-flash page index (0-based from 0x08000000) to the
 * (bank, page-in-bank) pair the STM32G0B1 flash erase controller expects.
 *
 * WHY THIS EXISTS: the G0B1 erase controller numbers pages PER BANK (0 ..
 * pages_per_bank-1) and selects the 128-page half with the BKER bit (i.e.
 * HAL Banks = FLASH_BANK_1 / FLASH_BANK_2), even when the option bytes put the
 * device in single-bank (linear-address) mode. `pages_per_bank` is the HAL's
 * FLASH_PAGE_NB: 128 when the part reports two banks, 256 when one. Passing a
 * flat page >= pages_per_bank with a hardcoded bank wraps the PNB field and
 * erases the WRONG physical page -- bench 2026-09-14, a slot-B erase (flat
 * page 128) with BANK_1/PNB=128 wrapped to PNB=0 and erased physical page 0,
 * wiping the bootloader and bricking the boot path twice. This mapping is the
 * fix; it is pure so it can be exhaustively host-tested.
 *
 * bank_out: 0 => bank 1 (BKER=0), 1 => bank 2 (BKER=1).
 * page_in_bank_out: the value for the CR PNB field (0 .. pages_per_bank-1). */
static inline void iflash_map_page(uint32_t flat_page, uint32_t pages_per_bank,
                                   uint32_t *bank_out, uint32_t *page_in_bank_out)
{
    if (flat_page < pages_per_bank) {
        *bank_out = 0u;
        *page_in_bank_out = flat_page;
    } else {
        *bank_out = 1u;
        *page_in_bank_out = flat_page - pages_per_bank;
    }
}

#endif /* IFLASH_PAGEMAP_H */
