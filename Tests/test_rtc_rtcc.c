#include "unity.h"
#include "rtc.h"
#include "fake_i2c.h"

static i2c_backend_t be;
void setUp(void) { fake_i2c_init(&be); rtc_init(&be); }
void tearDown(void) {}

static void test_stage_commit_then_get_live(void) {
    rtcc_set_year(25); rtcc_set_month(6); rtcc_set_day(15);
    rtcc_set_weekday(1); rtcc_set_hour(13); rtcc_set_minute(30); rtcc_set_second(45);
    TEST_ASSERT_EQUAL_INT(0, rtcc_commit());
    TEST_ASSERT_EQUAL_UINT16(25, rtcc_get_year());
    TEST_ASSERT_EQUAL_UINT16(6,  rtcc_get_month());
    TEST_ASSERT_EQUAL_UINT16(15, rtcc_get_day());
    TEST_ASSERT_EQUAL_UINT16(1,  rtcc_get_weekday());
    TEST_ASSERT_EQUAL_UINT16(13, rtcc_get_hour());
    TEST_ASSERT_EQUAL_UINT16(30, rtcc_get_minute());
    TEST_ASSERT_EQUAL_UINT16(45, rtcc_get_second());
}

static void test_stage_without_commit_does_not_change_clock(void) {
    /* Commit a known baseline. */
    rtcc_set_year(20); rtcc_set_month(1); rtcc_set_day(1);
    rtcc_set_weekday(1); rtcc_set_hour(0); rtcc_set_minute(0); rtcc_set_second(0);
    rtcc_commit();
    /* Stage a new year but do NOT commit. */
    rtcc_set_year(99);
    TEST_ASSERT_EQUAL_UINT16(20, rtcc_get_year());  /* live clock still baseline */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stage_commit_then_get_live);
    RUN_TEST(test_stage_without_commit_does_not_change_clock);
    return UNITY_END();
}
