#include "unity.h"
#include "control.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "mb_regmodel.h"

static apu_ctx_t ctx;
void setUp(void) { mb_reg_reset(); sensors_init(VREF_CAL_DEFAULT, 0); control_init(&ctx); control_regs_register(&ctx); }
void tearDown(void) {}

static void test_sample_copies_ext_temp_and_state(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_EXT, 1971); /* -> 32 degF, ON */
    control_sample_sensors(&ctx);
    TEST_ASSERT_EQUAL_INT16(32, ctx.external_temperature);
    TEST_ASSERT_EQUAL_UINT8(SENSOR_ON, ctx.ext_temp_sensor_state);
}

static void test_reg32_standby_override_rw(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(32, 1));
    TEST_ASSERT_TRUE(ctx.standby_override);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(32, &o));
    TEST_ASSERT_EQUAL_UINT16(1, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(32, 0));
    TEST_ASSERT_FALSE(ctx.standby_override);
}

static void test_sample_copies_cabin_temp(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_ENCL, 2048); /* ~25C midscale */
    control_sample_sensors(&ctx);
    TEST_ASSERT_EQUAL_INT16(sensors_get_encl_temp_f(), ctx.cabin_temperature);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sample_copies_ext_temp_and_state);
    RUN_TEST(test_reg32_standby_override_rw);
    RUN_TEST(test_sample_copies_cabin_temp);
    return UNITY_END();
}
