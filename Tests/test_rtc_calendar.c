#include "unity.h"
#include "rtc_calendar.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fake_nor.h"

static nvm_backend_t be;
void setUp(void) { fake_nor_init(&be); nvm_init(&be); }
void tearDown(void) {}

static void test_state_roundtrip_and_address(void) {
    rtc_cal_set_state(1);
    TEST_ASSERT_EQUAL_UINT8(1, rtc_cal_get_state());
    TEST_ASSERT_EQUAL_UINT8(1, nvm_read_byte(EE_CLND_START_ONOFF)); /* correct EE addr */
}

static void test_all_fields_roundtrip_distinct_addresses(void) {
    rtc_cal_set_mode(2);  rtc_cal_set_year(0x25); rtc_cal_set_month(6);
    rtc_cal_set_date(15); rtc_cal_set_hour(9);    rtc_cal_set_min(30);
    rtc_cal_set_ampm(1);
    TEST_ASSERT_EQUAL_UINT8(2,    rtc_cal_get_mode());
    TEST_ASSERT_EQUAL_UINT8(0x25, rtc_cal_get_year());
    TEST_ASSERT_EQUAL_UINT8(6,    rtc_cal_get_month());
    TEST_ASSERT_EQUAL_UINT8(15,   rtc_cal_get_date());
    TEST_ASSERT_EQUAL_UINT8(9,    rtc_cal_get_hour());
    TEST_ASSERT_EQUAL_UINT8(30,   rtc_cal_get_min());
    TEST_ASSERT_EQUAL_UINT8(1,    rtc_cal_get_ampm());
    /* distinct EE addresses (no aliasing) */
    TEST_ASSERT_EQUAL_UINT8(2,    nvm_read_byte(EE_CLND_START_MODE));
    TEST_ASSERT_EQUAL_UINT8(0x25, nvm_read_byte(EE_CLND_START_YEAR));
    TEST_ASSERT_EQUAL_UINT8(1,    nvm_read_byte(EE_CLND_START_AMPM));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_state_roundtrip_and_address);
    RUN_TEST(test_all_fields_roundtrip_distinct_addresses);
    return UNITY_END();
}
