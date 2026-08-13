#include "unity.h"
#include "control.h"

static apu_ctx_t ctx;
static int s_climate_calls, s_off_calls;
static void fake_climate(apu_ctx_t *c) { (void)c; s_climate_calls++; }
static void fake_off(apu_ctx_t *c) { (void)c; s_off_calls++; }

void setUp(void) {
    control_init(&ctx);
    s_climate_calls = 0; s_off_calls = 0;
    /* fresh registration each test: OP_OFF and OP_CLIMATE handlers */
    control_register_mode(OP_OFF, fake_off);
    control_register_mode(OP_CLIMATE, fake_climate);
}
void tearDown(void) {}

static void test_init_state(void) {
    TEST_ASSERT_EQUAL_INT(OP_POWER_UP, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(ERR_NONE, ctx.error_state);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
}

static void test_dispatch_calls_registered_handler(void) {
    ctx.op_state = OP_OFF;
    control_tick(&ctx);
    TEST_ASSERT_EQUAL_INT(1, s_off_calls);
    TEST_ASSERT_EQUAL_INT(0, s_climate_calls);
}

static void test_unregistered_op_state_is_safe_hold(void) {
    ctx.op_state = OP_ENGINE_START;    /* no handler registered */
    control_tick(&ctx);                /* must not crash, no dispatch */
    TEST_ASSERT_EQUAL_INT(0, s_off_calls);
    TEST_ASSERT_EQUAL_INT(0, s_climate_calls);
}

static void test_mode_request_change_transitions_op_state(void) {
    ctx.op_state = OP_OFF;
    ctx.mode_request = MODE_CLIMATE;   /* changed from init MODE_OFF */
    control_tick(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, ctx.op_state);  /* transition applied */
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);
    TEST_ASSERT_EQUAL_INT(1, s_climate_calls);        /* then dispatched climate */
}

static void test_mode_request_off_resets_error(void) {
    ctx.op_state = OP_CLIMATE; ctx.error_state = ERR_LOW_OIL;
    ctx.mode_request = MODE_CLIMATE; control_tick(&ctx);   /* establish prev=CLIMATE */
    ctx.mode_request = MODE_OFF;     control_tick(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(ERR_NONE, ctx.error_state);
}

static void test_no_transition_when_mode_unchanged(void) {
    ctx.op_state = OP_CLIMATE; ctx.mode_request = MODE_CLIMATE;
    control_tick(&ctx);                 /* prev becomes CLIMATE, dispatch climate */
    ctx.op_state = OP_ENGINE_START;     /* pretend climate handed off to engine-start */
    control_tick(&ctx);                 /* mode_request unchanged -> NO transition back */
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, ctx.op_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_state);
    RUN_TEST(test_dispatch_calls_registered_handler);
    RUN_TEST(test_unregistered_op_state_is_safe_hold);
    RUN_TEST(test_mode_request_change_transitions_op_state);
    RUN_TEST(test_mode_request_off_resets_error);
    RUN_TEST(test_no_transition_when_mode_unchanged);
    return UNITY_END();
}
