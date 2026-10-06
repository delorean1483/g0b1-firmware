/* Settings in the L011 data EEPROM: one word = magic, type, timeout, check. */
#include "port.h"
#include "stm32l0xx.h"

#define NV_ADDR  (DATA_EEPROM_BASE)
#define NV_MAGIC 0xA5u

static bool nv_load(void *ctx, uint8_t *type, uint8_t *timeout)
{
    uint32_t w = *(volatile const uint32_t *)NV_ADDR;
    uint8_t m = (uint8_t)w, t = (uint8_t)(w >> 8), to = (uint8_t)(w >> 16), c = (uint8_t)(w >> 24);
    (void)ctx;
    if ((m != NV_MAGIC) || (c != (uint8_t)(m ^ t ^ to ^ 0xFFu))) return false;
    *type = t;
    *timeout = to;
    return true;
}

static void nv_save(void *ctx, uint8_t type, uint8_t timeout)
{
    uint32_t w = NV_MAGIC | ((uint32_t)type << 8) | ((uint32_t)timeout << 16) |
                 ((uint32_t)(uint8_t)(NV_MAGIC ^ type ^ timeout ^ 0xFFu) << 24);
    (void)ctx;
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = 0x89ABCDEFu;
        FLASH->PEKEYR = 0x02030405u;
    }
    *(volatile uint32_t *)NV_ADDR = w;          /* auto-erase + program, ~3.2 ms */
    while (FLASH->SR & FLASH_SR_BSY) {}
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

const htr_nv_t port_nv = { nv_load, nv_save, 0 };
