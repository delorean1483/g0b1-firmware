#include "unity.h"
#include "sensors.h"
#include "sensors_cal.h"

void setUp(void)    { sensors_init(VREF_CAL_DEFAULT, 0); }
void tearDown(void) {}

/* Average only updates once the sample count is reached. */
static void test_batt_average_and_convert(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2374);
    TEST_ASSERT_EQUAL_UINT16(1200, sensors_get_batt_cv()); /* 12.00 V */
}

static void test_average_mean_of_samples(void) {
    /* Half at 2176 (11.0V), half at 2868 (14.5V) => mean 2522 => ~1275 cV. */
    for (int i = 0; i < SENS_AVG_DEFAULT / 2; i++) sensors_add_sample(SENS_BATT, 2176);
    for (int i = 0; i < SENS_AVG_DEFAULT / 2; i++) sensors_add_sample(SENS_BATT, 2868);
    TEST_ASSERT_UINT16_WITHIN(3, 1275, sensors_get_batt_cv());
}

static void test_partial_window_holds_previous(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2374);
    sensors_add_sample(SENS_BATT, 0);          /* 1 sample into next window */
    TEST_ASSERT_EQUAL_UINT16(1200, sensors_get_batt_cv()); /* unchanged until window full */
}

static void test_ext_adc_and_temp_and_state(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_EXT, 1971);
    TEST_ASSERT_EQUAL_UINT16(1971, sensors_get_ext_adc());   /* reg 3 raw avg */
    TEST_ASSERT_EQUAL_INT16(32, sensors_get_ext_temp_f());   /* reg 51 degF */
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, sensors_get_ext_state());
}

static void test_encl_channel(void) {
    for (int i = 0; i < SENS_AVG_ENCL; i++) sensors_add_sample(SENS_ENCL, 2048);
    TEST_ASSERT_EQUAL_INT16(77, sensors_get_encl_temp_f());  /* reg 1 */
}

static void test_set_cal_reconverts_on_next_window(void) {
    sensors_set_cal(VREF_CAL_DEFAULT, 5);                    /* +5 degF offset */
    for (int i = 0; i < SENS_AVG_ENCL; i++) sensors_add_sample(SENS_ENCL, 2048);
    TEST_ASSERT_EQUAL_INT16(82, sensors_get_encl_temp_f());  /* 77 + 5 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_batt_average_and_convert);
    RUN_TEST(test_average_mean_of_samples);
    RUN_TEST(test_partial_window_holds_previous);
    RUN_TEST(test_ext_adc_and_temp_and_state);
    RUN_TEST(test_encl_channel);
    RUN_TEST(test_set_cal_reconverts_on_next_window);
    return UNITY_END();
}
