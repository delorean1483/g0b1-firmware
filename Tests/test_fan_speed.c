#include "unity.h"
#include "fan_speed.h"

void setUp(void) {}
void tearDown(void) {}

/* percent 0..100 -> permille: 0 = off; 1..100 = [FAN_MIN_DUTY .. 1000] linear. */

static void test_off_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, fan_duty_permille(0));
}

static void test_one_percent_is_min_spin_floor(void) {
    TEST_ASSERT_EQUAL_UINT16(FAN_MIN_DUTY, fan_duty_permille(1));   /* 318 (32%) */
}

static void test_full_is_1000(void) {
    TEST_ASSERT_EQUAL_UINT16(1000, fan_duty_permille(100));
}

static void test_midpoint_is_linear(void) {
    TEST_ASSERT_EQUAL_UINT16(655, fan_duty_permille(50));           /* 318 + 49*682/99 */
}

static void test_over_100_clamps_to_full(void) {
    TEST_ASSERT_EQUAL_UINT16(1000, fan_duty_permille(200));         /* fail-safe: never under-drive */
}

static void test_monotonic_within_bounds(void) {
    uint16_t prev = 0;
    for (int p = 0; p <= 100; ++p) {
        uint16_t d = fan_duty_permille((uint8_t)p);
        TEST_ASSERT_TRUE(d >= prev);        /* non-decreasing */
        TEST_ASSERT_TRUE(d <= 1000u);       /* never exceeds full */
        prev = d;
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_off_is_zero);
    RUN_TEST(test_one_percent_is_min_spin_floor);
    RUN_TEST(test_full_is_1000);
    RUN_TEST(test_midpoint_is_linear);
    RUN_TEST(test_over_100_clamps_to_full);
    RUN_TEST(test_monotonic_within_bounds);
    return UNITY_END();
}
