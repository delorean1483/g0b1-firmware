#ifndef HTR_NV_MODE_H
#define HTR_NV_MODE_H
#include <stdint.h>
/* Additive EEPROM word at +4. The legacy word at +0 is never migrated or
   replaced, so a 0.2.3 rollback still reads its original type and timeout.
   Old/blank/invalid extension words always mean temperature mode. */
static inline uint32_t htr_nv_mode_encode(uint8_t mode)
{
    return 0x4D4F0000u | ((uint32_t)(uint8_t)~mode << 8) | mode;
}
static inline uint8_t htr_nv_mode_decode(uint32_t word)
{
    uint8_t mode = (uint8_t)word;
    return ((word >> 16) == 0x4D4Fu && mode <= 1u &&
            (uint8_t)(word >> 8) == (uint8_t)(mode ^ 0xFFu)) ? mode : 0u;
}
#endif
