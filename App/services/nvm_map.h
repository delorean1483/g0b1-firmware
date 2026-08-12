#ifndef NVM_MAP_H
#define NVM_MAP_H
/* Byte offsets within the parameter block — preserved from the PIC eeprom.h.
   Words are little-endian: low byte at OFFSET, high byte at OFFSET+1. */
#define NVM_PARAM_SIZE            256u

#define EE_VREF_CALIBRATION       0u   /* word */
#define EE_TEMP_CALIBRATION       2u   /* word */
#define MACHINE_RUNTIME_START     10u  /* word */
#define ENGINE_RUNTIME_START      12u  /* word */
#define ENGINE_OILTIME_START      14u  /* word */
#define EE_CLIMATE_TEMP_SETTING   20u  /* word */
#define EE_MONITOR_BATT_SETTING   22u  /* word */
#define EE_STORAGE_TEMP_SETTING   24u  /* word */
#define EE_STORAGE_BATT_SETTING   26u  /* word */
#define EE_EVAP_FAN_SPEED         30u  /* byte */
#define EE_TEMP_UNIT              31u  /* byte */
#define EEPROM_WRITTEN_FLAG       40u  /* sentinel byte = 0x55 */
#define EE_CLND_START_ONOFF       50u
#define EE_CLND_START_MODE        51u
#define EE_CLND_START_YEAR        52u
#define EE_CLND_START_MONTH       53u
#define EE_CLND_START_DATE        54u
#define EE_CLND_START_HOUR        55u
#define EE_CLND_START_MIN         56u
#define EE_CLND_START_AMPM        57u
#define EE_BOOTLOADER_FLAG        200u

#define EE_SENTINEL_VALUE         0x55u
#endif
