#include "unity.h"
#include "nvm_map.h"
#include "nvm_defaults.h"
static uint8_t sh[NVM_PARAM_SIZE];
void setUp(void) {} void tearDown(void) {}

static uint16_t w(const uint8_t *s, unsigned a){ return (uint16_t)(s[a] | (s[a+1] << 8)); }

static void test_defaults(void) {
    for (unsigned i=0;i<NVM_PARAM_SIZE;i++) sh[i]=0xAA;   /* poison first */
    nvm_apply_factory_defaults(sh);
    TEST_ASSERT_EQUAL_UINT16(250,  w(sh, EE_VREF_CALIBRATION));
    TEST_ASSERT_EQUAL_UINT16(0,    w(sh, EE_TEMP_CALIBRATION));
    TEST_ASSERT_EQUAL_UINT16(0,    w(sh, MACHINE_RUNTIME_START));
    TEST_ASSERT_EQUAL_UINT16(0,    w(sh, ENGINE_RUNTIME_START));
    TEST_ASSERT_EQUAL_UINT16(0,    w(sh, ENGINE_OILTIME_START));
    TEST_ASSERT_EQUAL_UINT16(70,   w(sh, EE_CLIMATE_TEMP_SETTING));
    TEST_ASSERT_EQUAL_UINT16(1200, w(sh, EE_MONITOR_BATT_SETTING));
    TEST_ASSERT_EQUAL_UINT16(30,   w(sh, EE_STORAGE_TEMP_SETTING));
    TEST_ASSERT_EQUAL_UINT16(1180, w(sh, EE_STORAGE_BATT_SETTING));
    TEST_ASSERT_EQUAL_HEX8(2,      sh[EE_EVAP_FAN_SPEED]);  /* HIGH */
    TEST_ASSERT_EQUAL_HEX8(0,      sh[EE_TEMP_UNIT]);       /* FAHRENHEIT */
    TEST_ASSERT_EQUAL_HEX8(0x55,   sh[EEPROM_WRITTEN_FLAG]);
    TEST_ASSERT_EQUAL_HEX8(0,      sh[EE_CLND_START_ONOFF]);
    TEST_ASSERT_EQUAL_HEX8(1,      sh[EE_CLND_START_MODE]);
    TEST_ASSERT_EQUAL_HEX8(0x13,   sh[EE_CLND_START_YEAR]);
    TEST_ASSERT_EQUAL_HEX8(0x01,   sh[EE_CLND_START_MONTH]);
    TEST_ASSERT_EQUAL_HEX8(0x10,   sh[EE_CLND_START_DATE]);
    TEST_ASSERT_EQUAL_HEX8(0x09,   sh[EE_CLND_START_HOUR]);
    TEST_ASSERT_EQUAL_HEX8(0x00,   sh[EE_CLND_START_MIN]);
    TEST_ASSERT_EQUAL_HEX8(0,      sh[EE_CLND_START_AMPM]);
    TEST_ASSERT_EQUAL_HEX8(0,      sh[EE_BOOTLOADER_FLAG]);
    TEST_ASSERT_EQUAL_HEX8(0,      sh[100]);               /* unused byte zeroed */
}
int main(void){ UNITY_BEGIN(); RUN_TEST(test_defaults); return UNITY_END(); }
