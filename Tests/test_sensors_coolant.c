/* Engine-coolant sender conversion on the AN_Ext_Temp net (PA4 / ADC1_IN4).
 *
 * Robb's board front-end (2026-09-02): 197.9 ohm pull-up to 3.300 V, read against
 * the 3.000 V ADC reference, 12-bit; Steinhart-Hart with the sender's A/B/C
 * coefficients. Expected degF below are computed from that reference function
 * (round to nearest). Note the ADC ref (3.000 V) is below the pull-up rail
 * (3.300 V), so only a dead short to ground (count 0 -> 0 V) reads as a fault;
 * an open sender rails near 32 degF rather than flagging.
 */
#include "unity.h"
#include "sensors.h"

void setUp(void) {}
void tearDown(void) {}

static void test_coolant_temp_from_counts(void) {
    uint8_t st = 0;
    TEST_ASSERT_EQUAL_INT16(215, sensors_coolant_temp_f(700,  &st)); /* 101.9 C */
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, st);
    TEST_ASSERT_EQUAL_INT16(188, sensors_coolant_temp_f(1000, &st)); /*  86.6 C */
    TEST_ASSERT_EQUAL_INT16(132, sensors_coolant_temp_f(2000, &st)); /*  55.5 C */
    TEST_ASSERT_EQUAL_INT16(82,  sensors_coolant_temp_f(3200, &st)); /*  27.8 C */
}

static void test_coolant_colder_as_counts_rise(void) {
    uint8_t st = 0;   /* NTC: higher count -> higher sender R -> colder */
    TEST_ASSERT_TRUE(sensors_coolant_temp_f(700, &st) > sensors_coolant_temp_f(3200, &st));
}

static void test_coolant_short_to_ground_is_fault(void) {
    uint8_t st = SENSOR_ON;
    int16_t f = sensors_coolant_temp_f(0, &st);   /* 0 V => short-to-ground rail */
    TEST_ASSERT_EQUAL_UINT8(SENSOR_OFF, st);
    TEST_ASSERT_TRUE(f < -400);                   /* fault sentinel, not a real temp */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_coolant_temp_from_counts);
    RUN_TEST(test_coolant_colder_as_counts_rise);
    RUN_TEST(test_coolant_short_to_ground_is_fault);
    return UNITY_END();
}
