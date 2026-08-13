#include "unity.h"
#include "rtc.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bcd_to_bin_known(void) {
    TEST_ASSERT_EQUAL_UINT8(0,  rtc_bcd_to_bin(0x00));
    TEST_ASSERT_EQUAL_UINT8(9,  rtc_bcd_to_bin(0x09));
    TEST_ASSERT_EQUAL_UINT8(23, rtc_bcd_to_bin(0x23));
    TEST_ASSERT_EQUAL_UINT8(59, rtc_bcd_to_bin(0x59));
    TEST_ASSERT_EQUAL_UINT8(99, rtc_bcd_to_bin(0x99));
}

static void test_bin_to_bcd_known(void) {
    TEST_ASSERT_EQUAL_UINT8(0x00, rtc_bin_to_bcd(0));
    TEST_ASSERT_EQUAL_UINT8(0x09, rtc_bin_to_bcd(9));
    TEST_ASSERT_EQUAL_UINT8(0x13, rtc_bin_to_bcd(13));
    TEST_ASSERT_EQUAL_UINT8(0x45, rtc_bin_to_bcd(45));
    TEST_ASSERT_EQUAL_UINT8(0x99, rtc_bin_to_bcd(99));
}

static void test_bcd_roundtrip_0_to_99(void) {
    for (uint8_t v = 0; v <= 99; v++)
        TEST_ASSERT_EQUAL_UINT8(v, rtc_bcd_to_bin(rtc_bin_to_bcd(v)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bcd_to_bin_known);
    RUN_TEST(test_bin_to_bcd_known);
    RUN_TEST(test_bcd_roundtrip_0_to_99);
    return UNITY_END();
}
