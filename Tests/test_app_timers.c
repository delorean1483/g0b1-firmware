#include "unity.h"
#include "app_timers.h"

void setUp(void) { app_timers_init(); }
void tearDown(void) {}

static void test_set_get_and_expired(void) {
    TEST_ASSERT_TRUE(app_timer_expired(SCALE_SECOND, POWER_UP_TMR));  /* 0 at init */
    app_timer_set(SCALE_SECOND, POWER_UP_TMR, 3);
    TEST_ASSERT_EQUAL_UINT16(3, app_timer_get(SCALE_SECOND, POWER_UP_TMR));
    TEST_ASSERT_FALSE(app_timer_expired(SCALE_SECOND, POWER_UP_TMR));
}

static void test_tick_decrements_to_zero_then_stops(void) {
    app_timer_set(SCALE_SECOND, BATT_STABLE_TMR, 2);
    app_timers_tick(SCALE_SECOND);
    TEST_ASSERT_EQUAL_UINT16(1, app_timer_get(SCALE_SECOND, BATT_STABLE_TMR));
    app_timers_tick(SCALE_SECOND);
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_SECOND, BATT_STABLE_TMR));
    TEST_ASSERT_TRUE(app_timer_expired(SCALE_SECOND, BATT_STABLE_TMR));
    app_timers_tick(SCALE_SECOND);                                    /* stays at 0 */
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_SECOND, BATT_STABLE_TMR));
}

static void test_tick_only_affects_its_scale(void) {
    app_timer_set(SCALE_SECOND, POWER_UP_TMR, 5);
    app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 5);
    app_timers_tick(SCALE_SECOND);
    TEST_ASSERT_EQUAL_UINT16(4, app_timer_get(SCALE_SECOND, POWER_UP_TMR));
    TEST_ASSERT_EQUAL_UINT16(5, app_timer_get(SCALE_MINUTE, DEFROST_CYCLE_TMR)); /* untouched */
}

static void test_multiple_timers_in_scale_decrement_together(void) {
    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 3);
    app_timer_set(SCALE_TEN_MS, RPM_STOP_TMR, 1);
    app_timers_tick(SCALE_TEN_MS);
    TEST_ASSERT_EQUAL_UINT16(2, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR));
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_TEN_MS, RPM_STOP_TMR));
}

static void test_out_of_range_safe(void) {
    app_timer_set(SCALE_TEN_MS, 99, 5);                    /* no-op, no crash */
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_TEN_MS, 99));
    TEST_ASSERT_TRUE(app_timer_expired(SCALE_TEN_MS, 99));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_and_expired);
    RUN_TEST(test_tick_decrements_to_zero_then_stops);
    RUN_TEST(test_tick_only_affects_its_scale);
    RUN_TEST(test_multiple_timers_in_scale_decrement_together);
    RUN_TEST(test_out_of_range_safe);
    return UNITY_END();
}
