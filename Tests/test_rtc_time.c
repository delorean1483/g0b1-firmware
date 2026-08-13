#include "unity.h"
#include "rtc.h"
#include "fake_i2c.h"

static i2c_backend_t be;
void setUp(void) { fake_i2c_init(&be); rtc_init(&be); }
void tearDown(void) {}

static void test_set_then_get_roundtrip(void) {
    rtc_time_t in = { .sec=45, .min=30, .hour=13, .weekday=1, .date=15, .month=6, .year=25 };
    rtc_time_t out = {0};
    TEST_ASSERT_EQUAL_INT(0, rtc_set_time(&in));
    TEST_ASSERT_EQUAL_INT(0, rtc_get_time(&out));
    TEST_ASSERT_EQUAL_UINT8(45, out.sec);
    TEST_ASSERT_EQUAL_UINT8(30, out.min);
    TEST_ASSERT_EQUAL_UINT8(13, out.hour);
    TEST_ASSERT_EQUAL_UINT8(1,  out.weekday);
    TEST_ASSERT_EQUAL_UINT8(15, out.date);
    TEST_ASSERT_EQUAL_UINT8(6,  out.month);
    TEST_ASSERT_EQUAL_UINT8(25, out.year);
}

static void test_set_time_encodes_bcd_and_control_bits(void) {
    rtc_time_t in = { .sec=45, .min=30, .hour=13, .weekday=1, .date=15, .month=6, .year=25 };
    rtc_set_time(&in);
    uint8_t *r = fake_i2c_raw();
    TEST_ASSERT_EQUAL_UINT8(0xC5, r[0x00]);            /* ST(0x80) | sec BCD 0x45 */
    TEST_ASSERT_EQUAL_UINT8(0x30, r[0x01]);            /* min BCD */
    TEST_ASSERT_EQUAL_UINT8(0x13, r[0x02] & 0x3F);     /* hour BCD, 24h (bit6 clear) */
    TEST_ASSERT_EQUAL_UINT8(0x00, r[0x02] & 0x40);     /* 24-hour mode */
    TEST_ASSERT_EQUAL_UINT8(0x08, r[0x03] & 0x08);     /* VBATEN set */
    TEST_ASSERT_EQUAL_UINT8(0x25, r[0x06]);            /* year BCD */
}

static void test_get_time_masks_control_bits(void) {
    uint8_t *r = fake_i2c_raw();
    r[0x00] = 0xC5;  /* ST(0x80) | sec BCD 0x45 */
    r[0x03] = 0x2B;  /* OSCRUN(0x20) | VBATEN(0x08) | weekday 3 */
    r[0x02] = 0x13;  /* hour 13, 24h */
    rtc_time_t out = {0};
    rtc_get_time(&out);
    TEST_ASSERT_EQUAL_UINT8(45, out.sec);      /* ST masked off */
    TEST_ASSERT_EQUAL_UINT8(3,  out.weekday);  /* OSCRUN/VBATEN masked off */
    TEST_ASSERT_EQUAL_UINT8(13, out.hour);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_then_get_roundtrip);
    RUN_TEST(test_set_time_encodes_bcd_and_control_bits);
    RUN_TEST(test_get_time_masks_control_bits);
    return UNITY_END();
}
