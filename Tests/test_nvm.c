#include "unity.h"
#include "fake_nor.h"
#include "nvm.h"
#include "nvm_map.h"
#include "nvm_record.h"   /* NVM_RECORD_SIZE, NVM_HEADER_SIZE for the torn-write test */
static nvm_backend_t be;
void setUp(void){ fake_nor_init(&be); } void tearDown(void){}

static void test_blank_device_factory_inits(void) {
    nvm_init(&be);
    TEST_ASSERT_EQUAL_HEX8(0x55, nvm_read_byte(EEPROM_WRITTEN_FLAG));
    TEST_ASSERT_EQUAL_UINT16(1200, nvm_read_word(EE_MONITOR_BATT_SETTING));
    TEST_ASSERT_EQUAL_UINT16(250,  nvm_read_word(EE_VREF_CALIBRATION));
}
static void test_write_persists_across_reinit(void) {
    nvm_init(&be);
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 64u);
    nvm_write_byte(EE_TEMP_UNIT, 1u);
    TEST_ASSERT_TRUE(nvm_dirty());
    TEST_ASSERT_EQUAL_INT(0, nvm_commit());
    TEST_ASSERT_FALSE(nvm_dirty());
    nvm_init(&be);                                   /* reload from flash */
    TEST_ASSERT_EQUAL_UINT16(64u, nvm_read_word(EE_CLIMATE_TEMP_SETTING));
    TEST_ASSERT_EQUAL_HEX8(1u,    nvm_read_byte(EE_TEMP_UNIT));
}
static void test_no_write_no_dirty(void) {
    nvm_init(&be);
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 70u);    /* 70 is already the default → unchanged */
    TEST_ASSERT_FALSE(nvm_dirty());
}
static void test_ring_rollover(void) {
    nvm_init(&be);                                   /* seq 1 at slot 0 */
    for (uint16_t i = 0; i < 200; i++) {             /* far more than slots_total (60) */
        nvm_write_word(MACHINE_RUNTIME_START, i);
        TEST_ASSERT_EQUAL_INT(0, nvm_commit());
    }
    nvm_init(&be);
    TEST_ASSERT_EQUAL_UINT16(199u, nvm_read_word(MACHINE_RUNTIME_START));
}
static void test_torn_last_record_recovers_previous(void) {
    nvm_init(&be);                                              /* seq1 @ slot0 (defaults) */
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 60u); nvm_commit(); /* seq2 @ slot1 */
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 61u); nvm_commit(); /* seq3 @ slot2 (recovery target) */
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 62u); nvm_commit(); /* seq4 @ slot3 (newest) */
    /* Simulate a torn write of the newest record: clear a payload bit at slot 3 so its
       CRC no longer matches. slot3 = 3*NVM_RECORD_SIZE, still within sector 0 (15 slots/sector).
       Corrupt byte 0 of payload (EE_VREF_CALIBRATION = 0xFA, has bit 7 set). */
    fake_nor_raw()[3u * NVM_RECORD_SIZE + NVM_HEADER_SIZE] &= 0x7F;
    nvm_init(&be);                                              /* slot3 invalid → falls back to slot2 (61) */
    TEST_ASSERT_EQUAL_UINT16(61u, nvm_read_word(EE_CLIMATE_TEMP_SETTING));
}
int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_blank_device_factory_inits);
    RUN_TEST(test_write_persists_across_reinit);
    RUN_TEST(test_no_write_no_dirty);
    RUN_TEST(test_ring_rollover);
    RUN_TEST(test_torn_last_record_recovers_previous);
    return UNITY_END();
}
