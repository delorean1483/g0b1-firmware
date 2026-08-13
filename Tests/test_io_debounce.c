#include "unity.h"
#include "io_debounce.h"

static discrete_input_t d;
void setUp(void) { io_debounce_init(&d, DEBOUNCE_TIME, SWITCH_OPEN); }
void tearDown(void) {}

static void test_init_state(void) {
    TEST_ASSERT_EQUAL_UINT8(SWITCH_OPEN, io_debounce_state(&d));
}

static void test_commits_after_debounce_time_stable_samples(void) {
    for (unsigned i = 0; i < DEBOUNCE_TIME - 1u; i++) {   /* 9 samples: not yet */
        io_debounce_service(&d, SWITCH_CLOSED);
        TEST_ASSERT_EQUAL_UINT8(SWITCH_OPEN, io_debounce_state(&d));
    }
    io_debounce_service(&d, SWITCH_CLOSED);               /* 10th: commit */
    TEST_ASSERT_EQUAL_UINT8(SWITCH_CLOSED, io_debounce_state(&d));
    TEST_ASSERT_EQUAL_UINT8(1u, d.service_needed);
}

static void test_bounce_shorter_than_window_rejected(void) {
    for (unsigned i = 0; i < 5; i++) io_debounce_service(&d, SWITCH_CLOSED);
    io_debounce_service(&d, SWITCH_OPEN);                 /* glitch resets the run */
    for (unsigned i = 0; i < 5; i++) io_debounce_service(&d, SWITCH_CLOSED);
    TEST_ASSERT_EQUAL_UINT8(SWITCH_OPEN, io_debounce_state(&d)); /* never 10 in a row */
}

static void test_stable_at_committed_value_no_service(void) {
    for (unsigned i = 0; i < DEBOUNCE_TIME; i++) io_debounce_service(&d, SWITCH_CLOSED);
    d.service_needed = 0;
    for (unsigned i = 0; i < 20; i++) io_debounce_service(&d, SWITCH_CLOSED); /* stays CLOSED */
    TEST_ASSERT_EQUAL_UINT8(SWITCH_CLOSED, io_debounce_state(&d));
    TEST_ASSERT_EQUAL_UINT8(0u, d.service_needed);        /* no new change */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_state);
    RUN_TEST(test_commits_after_debounce_time_stable_samples);
    RUN_TEST(test_bounce_shorter_than_window_rejected);
    RUN_TEST(test_stable_at_committed_value_no_service);
    return UNITY_END();
}
