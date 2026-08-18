/* Tach period -> RPM conversion (bench bring-up Task 9, host-tested slice).
 *
 * RPM = round(RPM_FREQ_MULT * timer_hz / period_ticks), RPM_FREQ_MULT = 6.
 * Expected values below assume the driver's 500 kHz capture timer:
 *   period = 6 * 500000 / RPM  ->  1000 RPM = 3000 ticks, 3000 RPM = 1000 ticks.
 */
#include "unity.h"
#include "rpm_calc.h"

#define THZ 500000u   /* TIM3 capture clock in the driver (64 MHz / 128) */

void setUp(void) {}
void tearDown(void) {}

static void test_typical_rpm_from_period(void) {
    TEST_ASSERT_EQUAL_UINT16(1000, rpm_from_period_ticks(3000, THZ)); /* 6*500k/3000 */
    TEST_ASSERT_EQUAL_UINT16(3000, rpm_from_period_ticks(1000, THZ)); /* 6*500k/1000 */
    TEST_ASSERT_EQUAL_UINT16(750,  rpm_from_period_ticks(4000, THZ)); /* 6*500k/4000 */
}

static void test_zero_period_is_guarded(void) {
    TEST_ASSERT_EQUAL_UINT16(0, rpm_from_period_ticks(0, THZ));       /* no divide-by-zero */
}

static void test_result_saturates_at_uint16(void) {
    /* Tiny period -> 3,000,000 RPM -> clamp, never wrap. */
    TEST_ASSERT_EQUAL_UINT16(65535, rpm_from_period_ticks(1, THZ));
}

static void test_rounds_to_nearest(void) {
    /* 3,000,000 / 6001 = 499.917 -> 500 rounded (truncation would give 499). */
    TEST_ASSERT_EQUAL_UINT16(500, rpm_from_period_ticks(6001, THZ));
}

static void test_low_end_near_16bit_overflow(void) {
    /* 3,000,000 / 65535 = 45.78 -> 46 (the practical floor before the 16-bit
       timer wraps; below this the driver falls back to the capture timeout). */
    TEST_ASSERT_EQUAL_UINT16(46, rpm_from_period_ticks(65535, THZ));
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
