#include "nvm_defaults.h"
#include "nvm_map.h"
#include <string.h>

static void wr16(uint8_t *s, unsigned a, uint16_t v) {
    s[a]     = (uint8_t)(v & 0xFF);
    s[a + 1] = (uint8_t)(v >> 8);
}
void nvm_apply_factory_defaults(uint8_t *shadow) {
    memset(shadow, 0, NVM_PARAM_SIZE);
    wr16(shadow, EE_VREF_CALIBRATION,     250u);
    wr16(shadow, EE_TEMP_CALIBRATION,       0u);
    wr16(shadow, MACHINE_RUNTIME_START,     0u);
    wr16(shadow, ENGINE_RUNTIME_START,      0u);
    wr16(shadow, ENGINE_OILTIME_START,      0u);
    wr16(shadow, EE_CLIMATE_TEMP_SETTING,  70u);
    wr16(shadow, EE_MONITOR_BATT_SETTING, 1200u);
    wr16(shadow, EE_STORAGE_TEMP_SETTING,  30u);
    wr16(shadow, EE_STORAGE_BATT_SETTING,1180u);
    shadow[EE_EVAP_FAN_SPEED]   = 100u; /* percent: full (was HIGH=2) */
    shadow[EE_TEMP_UNIT]        = 0u;   /* FAHRENHEIT */
    shadow[EE_CLND_START_ONOFF] = 0u;
    shadow[EE_CLND_START_MODE]  = 1u;   /* CLIMATE_CONTROL_MODE */
    shadow[EE_CLND_START_YEAR]  = 0x13u;
    shadow[EE_CLND_START_MONTH] = 0x01u;
    shadow[EE_CLND_START_DATE]  = 0x10u;
    shadow[EE_CLND_START_HOUR]  = 0x09u;
    shadow[EE_CLND_START_MIN]   = 0x00u;
    shadow[EE_CLND_START_AMPM]  = 0u;
    shadow[EE_BOOTLOADER_FLAG]  = 0u;
    shadow[EEPROM_WRITTEN_FLAG] = EE_SENTINEL_VALUE;  /* vestigial: init is gated by journal validity now; kept for map compatibility */
}
