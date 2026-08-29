#include "unity.h"
#include "mbp_nvm.h"
#include "mb_regmodel.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fake_nor.h"

static nvm_backend_t nor;
void setUp(void) { mb_reg_reset(); fake_nor_init(&nor); nvm_init(&nor); mbp_nvm_register(); }
void tearDown(void) {}

static void test_word_setting_roundtrip_and_address(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(13, 1250));  /* monitor batt */
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(13, &o));
    TEST_ASSERT_EQUAL_UINT16(1250, o);
    TEST_ASSERT_EQUAL_UINT16(1250, nvm_read_word(EE_MONITOR_BATT_SETTING));
}

static void test_counter_reg11_word(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(11, 5000));
    TEST_ASSERT_EQUAL_UINT16(5000, nvm_read_word(ENGINE_RUNTIME_START));
}

static void test_byte_reg_and_range(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(19, 1));     /* temp unit ok */
    TEST_ASSERT_EQUAL_UINT8(1, nvm_read_byte(EE_TEMP_UNIT));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(19, 2));   /* out of range */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(12, 101)); /* fan speed > 100 % */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(12, 100));          /* fan speed 100 % ok */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(12, 0));            /* fan off ok */
}

static void test_calibration_regs(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(36, 260));
    TEST_ASSERT_EQUAL_UINT16(260, nvm_read_word(EE_VREF_CALIBRATION));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(37, 3));
    TEST_ASSERT_EQUAL_UINT16(3, nvm_read_word(EE_TEMP_CALIBRATION));
}

static void test_persist_across_reinit(void) {
    mb_reg_write(14, 70);                                        /* climate temp */
    nvm_init(&nor);                                             /* reload from backing store */
    TEST_ASSERT_EQUAL_UINT16(70, nvm_read_word(EE_CLIMATE_TEMP_SETTING));
}

/* A failed NOR persist must reach the Modbus master as SLAVE_DEVICE_FAILURE,
   not be silently swallowed (retires the M4b carry-forward). */
static void test_commit_failure_surfaces_slave_device_failure(void) {
    fake_nor_fail_writes(1);                                    /* program/erase now fail */
    TEST_ASSERT_EQUAL_INT(MB_EXC_SLAVE_DEVICE_FAILURE, mb_reg_write(13, 1250));
    fake_nor_fail_writes(0);                                    /* recover -> writes succeed again */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(13, 1250));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_word_setting_roundtrip_and_address);
    RUN_TEST(test_counter_reg11_word);
    RUN_TEST(test_byte_reg_and_range);
    RUN_TEST(test_calibration_regs);
    RUN_TEST(test_persist_across_reinit);
    RUN_TEST(test_commit_failure_surfaces_slave_device_failure);
    return UNITY_END();
}
