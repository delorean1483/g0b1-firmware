/* Tach period -> RPM conversion (bench bring-up Task 9, host-tested slice).
 *
 * RPM = round(RPM_FREQ_MULT * timer_hz / period_ticks), RPM_FREQ_MULT = 6.8125
 * (bench-calibrated 2026-09-02, OI-4; kept as 109/16 for integer-exact math).
 * Expected values below assume the driver's 500 kHz capture timer:
 *   num = 6.8125 * 500000 = 3,406,250, RPM = round(num / period_ticks).
 */
#include "unity.h"
#include "rpm_calc.h"

#define THZ 500000u   /* TIM3 capture clock in the driver (64 MHz / 128) */

void setUp(void) {}
void tearDown(void) {}

static void test_typical_rpm_from_period(void) {
    TEST_ASSERT_EQUAL_UINT16(1135, rpm_from_period_ticks(3000, THZ)); /* 3,406,250/3000 = 1135.4 */
    TEST_ASSERT_EQUAL_UINT16(3406, rpm_from_period_ticks(1000, THZ)); /* 3,406,250/1000 = 3406.25 */
    TEST_ASSERT_EQUAL_UINT16(852,  rpm_from_period_ticks(4000, THZ)); /* 3,406,250/4000 = 851.6 */
}

static void test_zero_period_is_guarded(void) {
    TEST_ASSERT_EQUAL_UINT16(0, rpm_from_period_ticks(0, THZ));       /* no divide-by-zero */
}

static void test_result_saturates_at_uint16(void) {
    /* Tiny period -> 3,406,250 RPM -> clamp, never wrap. */
    TEST_ASSERT_EQUAL_UINT16(65535, rpm_from_period_ticks(1, THZ));
}

static void test_rounds_to_nearest(void) {
    /* 3,406,250 / 6001 = 567.61 -> 568 rounded (truncation would give 567). */
    TEST_ASSERT_EQUAL_UINT16(568, rpm_from_period_ticks(6001, THZ));
}

static void test_low_end_near_16bit_overflow(void) {
    /* 3,406,250 / 65535 = 51.98 -> 52 (the practical floor before the 16-bit
       timer wraps; below this the driver falls back to the capture timeout). */
    TEST_ASSERT_EQUAL_UINT16(52, rpm_from_period_ticks(65535, THZ));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_typical_rpm_from_period);
    RUN_TEST(test_zero_period_is_guarded);
    RUN_TEST(test_result_saturates_at_uint16);
    RUN_TEST(test_rounds_to_nearest);
    RUN_TEST(test_low_end_near_16bit_overflow);
    return UNITY_END();
}
