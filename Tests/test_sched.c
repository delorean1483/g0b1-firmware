#include "unity.h"
#include "sched.h"
#include "app_timers.h"

static uint32_t s_calls[SLOT_COUNT];
static void h_10ms(void)  { s_calls[SLOT_10MS]++; }
static void h_50ms(void)  { s_calls[SLOT_50MS]++; }
static void h_100ms(void) { s_calls[SLOT_100MS]++; }
static void h_1s(void)    { s_calls[SLOT_1S]++; }
static void h_5s(void)    { s_calls[SLOT_5S]++; }
static void h_1min(void)  { s_calls[SLOT_1MIN]++; }

void setUp(void) {
    sched_init();
    for (int i = 0; i < SLOT_COUNT; i++) s_calls[i] = 0;
    sched_register(SLOT_10MS, h_10ms);   sched_register(SLOT_50MS, h_50ms);
    sched_register(SLOT_100MS, h_100ms); sched_register(SLOT_1S, h_1s);
    sched_register(SLOT_5S, h_5s);       sched_register(SLOT_1MIN, h_1min);
}
void tearDown(void) {}

/* Advance `total` ms in `step`-ms increments, running the scheduler each step. */
static void advance(uint16_t total, uint16_t step) {
    for (uint16_t t = 0; t < total; t += step) { sched_service(step); sched_run(); }
}

static void test_10ms_slot_fires_each_10ms(void) {
    advance(100, 1);                       /* 100 ms in 1 ms steps */
    TEST_ASSERT_EQUAL_UINT32(10, s_calls[SLOT_10MS]);
    TEST_ASSERT_EQUAL_UINT32(2,  s_calls[SLOT_50MS]);
    TEST_ASSERT_EQUAL_UINT32(1,  s_calls[SLOT_100MS]);
    TEST_ASSERT_EQUAL_UINT32(0,  s_calls[SLOT_1S]);
}

static void test_second_and_minute_cadence(void) {
    advance(60000, 10);                    /* 60 s in 10 ms steps */
    TEST_ASSERT_EQUAL_UINT32(60,   s_calls[SLOT_1S]);
    TEST_ASSERT_EQUAL_UINT32(12,   s_calls[SLOT_5S]);
    TEST_ASSERT_EQUAL_UINT32(1,    s_calls[SLOT_1MIN]);
    TEST_ASSERT_EQUAL_UINT32(6000, s_calls[SLOT_10MS]);
}

static void test_scheduler_ticks_timer_scales(void) {
    app_timer_set(SCALE_SECOND, POWER_UP_TMR, 3);
    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 5);
    advance(1000, 1);                      /* 1 s: 100 ten-ms ticks, 1 second tick */
    TEST_ASSERT_EQUAL_UINT16(2, app_timer_get(SCALE_SECOND, POWER_UP_TMR)); /* 3 - 1 */
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR)); /* floored */
}

static void test_due_flag_collapses_but_timer_accurate_under_jitter(void) {
    /* One big 30 ms step: SLOT_10MS handler runs once (flag collapsed), but the
       10 ms timer scale decrements 3 times (accuracy preserved). */
    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 10);
    sched_service(30); sched_run();
    TEST_ASSERT_EQUAL_UINT32(1, s_calls[SLOT_10MS]);                       /* collapsed */
    TEST_ASSERT_EQUAL_UINT16(7, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR)); /* 10 - 3 */
}

static void test_unregistered_slot_is_safe(void) {
    sched_init();                          /* no handlers registered */
    sched_service(10); sched_run();        /* must not crash */
    TEST_ASSERT_TRUE(true);
}

/* Bug: s_ms was a free-running uint32 that wraps at 2^32. Because 2^32 is not a
   multiple of any slot period and 0 % anything == 0, the wrap tick fired all six
   slots at once (~49.7-day uptime glitch). The fix re-phases s_ms at 60000 ms
   (the LCM of all slot periods) so the counter can never approach 2^32. Reaching
   2^32 in a test is impractical, so this pins the root-cause invariant: the
   counter stays bounded. */
static void test_ms_counter_rephases_no_32bit_wrap(void) {
    for (uint32_t t = 0; t < 130000u; t += 10u) { sched_service(10); sched_run(); }
    TEST_ASSERT_LESS_THAN_UINT32(60000u, sched_now_ms());
}

/* The re-phase must not disturb slot cadence across the 60000 ms boundary: after
   two full minutes the counts must be exact (a re-phase value that isn't a common
   multiple of every period would shift the boundaries). */
static void test_cadence_preserved_across_rephase(void) {
    for (uint32_t t = 0; t < 120000u; t += 10u) { sched_service(10); sched_run(); }
    TEST_ASSERT_EQUAL_UINT32(2,     s_calls[SLOT_1MIN]);
    TEST_ASSERT_EQUAL_UINT32(120,   s_calls[SLOT_1S]);
    TEST_ASSERT_EQUAL_UINT32(24,    s_calls[SLOT_5S]);
    TEST_ASSERT_EQUAL_UINT32(12000, s_calls[SLOT_10MS]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_10ms_slot_fires_each_10ms);
    RUN_TEST(test_second_and_minute_cadence);
    RUN_TEST(test_scheduler_ticks_timer_scales);
    RUN_TEST(test_due_flag_collapses_but_timer_accurate_under_jitter);
    RUN_TEST(test_unregistered_slot_is_safe);
    RUN_TEST(test_ms_counter_rephases_no_32bit_wrap);
    RUN_TEST(test_cadence_preserved_across_rephase);
    return UNITY_END();
}
