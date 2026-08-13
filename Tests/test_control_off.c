#include "unity.h"
#include "control.h"

static apu_ctx_t ctx;
void setUp(void) { control_init(&ctx); ctx.op_state = OP_OFF; }
void tearDown(void) {}

static void test_off_clears_outputs_and_error(void) {
    ctx.out.compressor_clutch = true; ctx.out.evap_fan = true;
    ctx.error_state = ERR_LOW_OIL; ctx.control_status = ST_COOLING;
    ctx.evap_fan_always_on = true;
    control_off_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    TEST_ASSERT_EQUAL_UINT8(ERR_NONE, ctx.error_state);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.engine_op_status);
    TEST_ASSERT_FALSE(ctx.evap_fan_always_on);
}

static void test_off_holds_op_state(void) {
    ctx.op_state = OP_CLIMATE;          /* seed a non-OFF state */
    control_off_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, ctx.op_state);  /* handler must not self-transition */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_off_clears_outputs_and_error);
    RUN_TEST(test_off_holds_op_state);
    return UNITY_END();
}
