#include "unity.h"
#include "control.h"

static apu_ctx_t ctx;
void setUp(void) { control_init(&ctx); }
void tearDown(void) {}

static void test_err_standby_value(void) {
    TEST_ASSERT_EQUAL_INT(7, ERR_STANDBY);           /* PIC STANDBY = 7 */
}

static void test_init_resets_es_fields(void) {
    TEST_ASSERT_EQUAL_UINT8(OP_OFF, ctx.op_state_previous);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
    TEST_ASSERT_EQUAL_INT16(0, ctx.external_temperature);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.ext_temp_sensor_state);
    TEST_ASSERT_TRUE(ctx.engine_temp_ok);            /* default OK until sensor wired */
    TEST_ASSERT_FALSE(ctx.standby_override);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_err_standby_value);
    RUN_TEST(test_init_resets_es_fields);
    return UNITY_END();
}
