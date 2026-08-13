#include "unity.h"
#include "sensors.h"
#include "sensors_cal.h"

void setUp(void) {}
void tearDown(void) {}

/* Schematic legend golden points at nominal calibration (vref_cal=250). */
static void test_battery_legend_points(void) {
    TEST_ASSERT_EQUAL_UINT16(1100, sensors_battery_cv(2176, VREF_CAL_DEFAULT)); /* 11.00 V */
    TEST_ASSERT_EQUAL_UINT16(1200, sensors_battery_cv(2374, VREF_CAL_DEFAULT)); /* 12.00 V */
    TEST_ASSERT_EQUAL_UINT16(1450, sensors_battery_cv(2868, VREF_CAL_DEFAULT)); /* 14.50 V */
}

static void test_battery_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, sensors_battery_cv(0, VREF_CAL_DEFAULT));
}

/* Trim scales linearly: +4% vref_cal => +4% reading. */
static void test_battery_vref_trim(void) {
    uint16_t nom = sensors_battery_cv(2374, 250);
    uint16_t hi  = sensors_battery_cv(2374, 260);
    TEST_ASSERT_EQUAL_UINT16(1200, nom);
    TEST_ASSERT_UINT16_WITHIN(2, 1248, hi); /* 1200 * 260/250 = 1248 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_legend_points);
    RUN_TEST(test_battery_zero);
    RUN_TEST(test_battery_vref_trim);
    return UNITY_END();
}
