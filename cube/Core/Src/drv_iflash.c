/* drv_iflash.c — STM32G0 internal-flash implementation of iflash_backend_t
 * (Task 7). Consumed by the bootloader (boot/) and the app's boot-config
 * write-on-confirm path.
 *
 * program() and erase() are marked __attribute__((section(".RamFunc"))) and
 * therefore execute out of RAM (see STM32G0B1RETX_FLASH.ld + the startup
 * .RamFunc copy loop). The G0B1 is single-bank flash: while a program/erase
 * operation is in progress the flash cannot service instruction fetches, so
 * any code that drives that operation — and can be reached while it runs —
 * must not itself reside in flash.
 *
 * read() is a plain memory-mapped copy; it never runs during a program/erase
 * (never called from inside program()/erase()) so it is safe to leave in
 * FLASH.
 */
#include "drv_iflash.h"
#include "stm32g0xx_hal.h"

#define FL_BASE   0x08000000u
#define FL_PAGE   2048u
#define FL_PAGES  256u

extern IWDG_HandleTypeDef hiwdg;   /* armed by the bootloader/app before flash ops */

/* read is a plain memory-mapped copy (safe from flash). */
static int iflash_read(void *c, uint32_t a, uint8_t *b, uint32_t n) {
    (void)c;
    if (a < FL_BASE || a + n > FL_BASE + FL_PAGES * FL_PAGE) return -1;
    const uint8_t *p = (const uint8_t *)a;
    for (uint32_t i = 0; i < n; i++) b[i] = p[i];
    return 0;
}

/* program: MUST run from RAM (single-bank read-while-write stall). */
__attribute__((section(".RamFunc")))
static int iflash_program(void *c, uint32_t a, const uint8_t *b, uint32_t n) {
    (void)c;
    if ((n & 7u) != 0u || (a & 7u) != 0u) return -1;
    if (a < FL_BASE || a + n > FL_BASE + FL_PAGES * FL_PAGE) return -1;
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < n; i += 8u) {
        uint64_t dw = 0;
        for (int k = 0; k < 8; k++) dw |= (uint64_t)b[i + k] << (8 * k);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, a + i, dw) != HAL_OK) {
            HAL_FLASH_Lock(); return -2;
        }
    }
    HAL_FLASH_Lock();
    return 0;
}

/* erase one page: MUST run from RAM; refresh IWDG so a 112-page slot erase
   (~3 s total) does not trip the ~2 s watchdog between pages. */
__attribute__((section(".RamFunc")))
static int iflash_erase(void *c, uint32_t page) {
    (void)c;
    if (page >= FL_PAGES) return -1;
    HAL_IWDG_Refresh(&hiwdg);
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef e = {0};
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Page = page;
    e.NbPages = 1;
    uint32_t err = 0;
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();
    /* stm32g0xx_hal_flash_ex.c: HAL_FLASHEx_Erase() sets *PageError = 0xFFFFFFFF
       before the erase loop and only overwrites it with the faulting page index
       on failure, so 0xFFFFFFFF unchanged == full success. Verified against the
       local HAL source (cube/Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash_ex.c). */
    return (st == HAL_OK && err == 0xFFFFFFFFu) ? 0 : -2;
}

static const iflash_backend_t s_be = {
    FL_PAGE, FL_PAGES, FL_BASE, iflash_read, iflash_program, iflash_erase, 0
};
const iflash_backend_t *drv_iflash_backend(void) { return &s_be; }
