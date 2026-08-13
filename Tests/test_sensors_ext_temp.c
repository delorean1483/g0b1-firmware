#include "unity.h"
#include "sensors.h"
#include "sensors_cal.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ext_table_breakpoints(void) {
    uint8_t st;
    TEST_ASSERT_EQUAL_INT16(32, sensors_ext_temp_f(1971, VREF_CAL_DEFAULT, &st)); /* 32 degF */
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, st);
    TEST_ASSERT_EQUAL_INT16(68, sensors_ext_temp_f(1074, VREF_CAL_DEFAULT, &st)); /* 68 degF */
    TEST_ASSERT_EQUAL_INT16(212, sensors_ext_temp_f(264, VREF_CAL_DEFAULT, &st)); /* 212 degF breakpoint */
}

static void test_ext_interpolation_midpoint(void) {
    uint8_t st;
    /* Between {1971,32} and {1518,50}: midpoint count 1744 -> ~41 degF. */
    int16_t f = sensors_ext_temp_f(1744, VREF_CAL_DEFAULT, &st);
    TEST_ASSERT_UINT16_WITHIN(1, 41, (uint16_t)f);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, st);
}

static void test_ext_disconnect(void) {
    uint8_t st;
    int16_t f = sensors_ext_temp_f(4095, VREF_CAL_DEFAULT, &st); /* saturated / open */
    TEST_ASSERT_EQUAL_INT16(0, f);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_OFF, st);
}

static void test_ext_short(void) {
    uint8_t st;
    int16_t f = sensors_ext_temp_f(0, VREF_CAL_DEFAULT, &st);
    TEST_ASSERT_EQUAL_INT16(0, f);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_OFF, st);
}

static void test_ext_cold_floor(void) {
    uint8_t st;
    /* Between floor (3709) and disconnect (4090): floored at -4 degF, still ON. */
    int16_t f = sensors_ext_temp_f(3900, VREF_CAL_DEFAULT, &st);
    TEST_ASSERT_EQUAL_INT16(NTC_FLOOR_F, f);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, st);
}

static void test_ext_overmax_off(void) {
    uint8_t st;
    int16_t f = sensors_ext_temp_f(200, VREF_CAL_DEFAULT, &st); /* < overmax count => hotter than 248 */
    TEST_ASSERT_EQUAL_INT16(0, f);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_OFF, st);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ext_table_breakpoints);
    RUN_TEST(test_ext_interpolation_midpoint);
    RUN_TEST(test_ext_disconnect);
    RUN_TEST(test_ext_short);
    RUN_TEST(test_ext_cold_floor);
    RUN_TEST(test_ext_overmax_off);
    return UNITY_END();
}
