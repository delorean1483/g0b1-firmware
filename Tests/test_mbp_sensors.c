#include "unity.h"
#include "mbp_sensors.h"
#include "mb_regmodel.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "rpm.h"

static uint16_t s_rpm;
static uint16_t fake_rpm(void *ctx) { (void)ctx; return s_rpm; }
static rpm_source_t src = { fake_rpm, 0 };

void setUp(void) {
    mb_reg_reset();
    sensors_init(VREF_CAL_DEFAULT, 0);
    mbp_sensors_register(&src);
}
void tearDown(void) {}

static void test_battery_reg6(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2374); /* 12.00 V */
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(6, &o));
    TEST_ASSERT_EQUAL_UINT16(1200, o);
}

static void test_ext_adc_reg3_and_temp_reg51(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_EXT, 1971);
    uint16_t adc = 0, f = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(3, &adc));
    TEST_ASSERT_EQUAL_UINT16(1971, adc);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(51, &f));
    TEST_ASSERT_EQUAL_UINT16(32, f);
}

static void test_rpm_reg38(void) {
    s_rpm = 2400;
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(38, &o));
    TEST_ASSERT_EQUAL_UINT16(2400, o);
}

static void test_sensors_are_read_only(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(6, 1234)); /* no write_fn */
}

static void test_encl_reg1_negative_twos_complement(void) {
    for (int i = 0; i < SENS_AVG_ENCL; i++) sensors_add_sample(SENS_ENCL, 1665); /* -> -40 degF */
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(1, &o));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(int16_t)(-40), o);   /* 0xFFD8 */
}

static void test_all_sensor_regs_reject_writes(void) {
    uint16_t regs[] = { 1, 3, 6, 38, 51 };
    for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++)
        TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(regs[i], 123));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_battery_reg6);
    RUN_TEST(test_ext_adc_reg3_and_temp_reg51);
    RUN_TEST(test_rpm_reg38);
    RUN_TEST(test_sensors_are_read_only);
    RUN_TEST(test_encl_reg1_negative_twos_complement);
    RUN_TEST(test_all_sensor_regs_reject_writes);
    return UNITY_END();
}
