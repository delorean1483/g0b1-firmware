#include "unity.h"
#include "control.h"
#include "fan_speed.h"

static apu_ctx_t ctx;
void setUp(void) { control_init(&ctx); }
void tearDown(void) {}

static void test_init_resets_cc_fields(void) {
    TEST_ASSERT_EQUAL_INT16(0, ctx.cabin_temperature);
    TEST_ASSERT_EQUAL_INT16(0, ctx.clmt_temp_setting);
    TEST_ASSERT_EQUAL_UINT8(100, ctx.evap_fan_speed);   /* percent: default full */
    TEST_ASSERT_EQUAL_UINT8(0, ctx.compressor_on_timer);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.compressor_off_timer);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.refregerant_check_counter);
    TEST_ASSERT_TRUE(ctx.ac_low_pressure_ok);
    TEST_ASSERT_TRUE(ctx.ac_high_pressure_ok);
    TEST_ASSERT_FALSE(ctx.cool_mode);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_resets_cc_fields);
    return UNITY_END();
}
