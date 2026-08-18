/* Hardening tests for sensors_add_sample (bench bring-up Task 8, Step 1-2).
 *
 * These pin the two documented input hazards the ADC feed exposes once real
 * DMA drives sensors_add_sample():
 *   1. divide-by-zero when a sample arrives before sensors_init() (window==0);
 *   2. out-of-bounds index when ch >= SENS_CH_COUNT.
 *
 * On ARM64 a bare integer divide-by-zero silently yields 0 (no trap), and an
 * out-of-range static-array write is undefined-but-quiet, so neither hazard is
 * observable through the public API. This executable is therefore built with
 * ASan+UBSan (-fno-sanitize-recover): without the guard the first hazard aborts
 * the process (RED); with the guard both calls return early (GREEN).
 */
#include "unity.h"
#include "sensors.h"
#include "sensors_cal.h"

void setUp(void)    {}   /* no init here: the pre-init test needs window==0 (statics start zero) */
void tearDown(void) {}

/* Must run FIRST, before any sensors_init(), so the BATT window is still 0.
 * RED: sensors_add_sample divides accum by window==0 -> UBSan divide-by-zero.
 * GREEN: the window==0 guard returns early; batt reading stays 0. */
static void test_add_sample_before_init_does_not_divide_by_zero(void) {
    sensors_add_sample(SENS_BATT, 5000);
    TEST_ASSERT_EQUAL_UINT16(0, sensors_get_batt_cv());
}

/* RED: ch==SENS_CH_COUNT indexes s_chan[3] past the 3-element array ->
 * ASan global-buffer-overflow. GREEN: the range guard returns early; the
 * previously-averaged valid channel is untouched. */
static void test_add_sample_out_of_range_channel_is_ignored(void) {
    sensors_init(VREF_CAL_DEFAULT, 0);
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2374);
    uint16_t before = sensors_get_batt_cv();                 /* 1200 cV */
    sensors_add_sample((sensor_ch_t)SENS_CH_COUNT, 9999);    /* out of range */
    TEST_ASSERT_EQUAL_UINT16(before, sensors_get_batt_cv());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_add_sample_before_init_does_not_divide_by_zero);  /* first: needs window==0 */
    RUN_TEST(test_add_sample_out_of_range_channel_is_ignored);
    return UNITY_END();
}
