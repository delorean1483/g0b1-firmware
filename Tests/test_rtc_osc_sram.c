#include "unity.h"
#include "rtc.h"
#include "fake_i2c.h"

static i2c_backend_t be;
void setUp(void) { fake_i2c_init(&be); rtc_init(&be); }
void tearDown(void) {}

static void test_osc_start_sets_st_and_reports_running(void) {
    TEST_ASSERT_FALSE(rtc_osc_running());          /* fresh regs: OSCRUN clear */
    TEST_ASSERT_EQUAL_INT(0, rtc_osc_start());
    TEST_ASSERT_EQUAL_UINT8(0x80, fake_i2c_raw()[0x00] & 0x80); /* ST set */
    TEST_ASSERT_TRUE(rtc_osc_running());            /* fake mirrors ST -> OSCRUN */
}

static void test_backup_enable_sets_vbaten(void) {
    TEST_ASSERT_EQUAL_INT(0, rtc_backup_enable());
    TEST_ASSERT_EQUAL_UINT8(0x08, fake_i2c_raw()[0x03] & 0x08);
}

static void test_sram_roundtrip(void) {
    uint8_t out[4] = {0xDE,0xAD,0xBE,0xEF};
    uint8_t in[4] = {0};
    TEST_ASSERT_EQUAL_INT(0, rtc_sram_write(0, out, 4));
    TEST_ASSERT_EQUAL_UINT8(0xDE, fake_i2c_raw()[0x20]);   /* SRAM base */
    TEST_ASSERT_EQUAL_INT(0, rtc_sram_read(0, in, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 4);
}

static void test_reg52_reads_sram_offset0(void) {
    uint8_t v = 0x55;
    rtc_sram_write(0, &v, 1);
    TEST_ASSERT_EQUAL_UINT8(0x55, rtc_reg52_read());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_osc_start_sets_st_and_reports_running);
    RUN_TEST(test_backup_enable_sets_vbaten);
    RUN_TEST(test_sram_roundtrip);
    RUN_TEST(test_reg52_reads_sram_offset0);
    return UNITY_END();
}
