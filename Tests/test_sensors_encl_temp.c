#include "unity.h"
#include "sensors.h"
#include "sensors_cal.h"

void setUp(void) {}
void tearDown(void) {}

static void test_encl_25c_anchor(void) {
    /* S=1.0 -> 2048 counts -> 25 C -> 77 degF (exact). */
    TEST_ASSERT_EQUAL_INT16(77, sensors_encl_temp_f(2048, 0));
}

static void test_encl_monotonic_increasing(void) {
    /* Higher count (hotter PTC) => higher degF. */
    TEST_ASSERT_TRUE(sensors_encl_temp_f(2361, 0) > sensors_encl_temp_f(2048, 0));
    TEST_ASSERT_TRUE(sensors_encl_temp_f(2048, 0) > sensors_encl_temp_f(1665, 0));
}

static void test_encl_calibration_offset(void) {
    int16_t base = sensors_encl_temp_f(2048, 0);
    TEST_ASSERT_EQUAL_INT16(base + 5, sensors_encl_temp_f(2048, 5));
}

static void test_encl_clamp_high(void) {
    TEST_ASSERT_EQUAL_INT16(TEMP_CLAMP_MAX_F, sensors_encl_temp_f(2533, 100)); /* 257+100 clamped */
}

static void test_encl_clamp_low(void) {
    TEST_ASSERT_EQUAL_INT16(TEMP_CLAMP_MIN_F, sensors_encl_temp_f(1665, -50)); /* -40-50 clamped */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encl_25c_anchor);
    RUN_TEST(test_encl_monotonic_increasing);
    RUN_TEST(test_encl_calibration_offset);
    RUN_TEST(test_encl_clamp_high);
    RUN_TEST(test_encl_clamp_low);
    return UNITY_END();
}
