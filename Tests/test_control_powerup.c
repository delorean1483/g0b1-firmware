#include "unity.h"
#include "control.h"
#include "app_timers.h"

static apu_ctx_t ctx;
void setUp(void) { app_timers_init(); control_init(&ctx); ctx.op_state = OP_POWER_UP; }
void tearDown(void) {}

static void test_entry_sets_timer_and_outputs_off(void) {
    ctx.out.fuel_pump = true;                     /* pretend stale */
    control_powerup_mode(&ctx);                   /* sub_state 0 */
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT16(1, app_timer_get(SCALE_SECOND, POWER_UP_TMR));
    TEST_ASSERT_EQUAL_UINT16(10, app_timer_get(SCALE_MINUTE, CABIN_TEMP_WARMUP_TMR));
    TEST_ASSERT_EQUAL_UINT8(1, ctx.sub_state);
}

static void test_holds_until_timer_expires_then_off(void) {
    control_powerup_mode(&ctx);                   /* sub 0 -> sets timer=1, sub=1 */
    control_powerup_mode(&ctx);                   /* sub 1, timer still 1 -> hold */
    TEST_ASSERT_EQUAL_INT(OP_POWER_UP, ctx.op_state);
    app_timers_tick(SCALE_SECOND);                /* timer 1 -> 0 */
    control_powerup_mode(&ctx);                   /* sub 1, expired -> OFF */
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_entry_sets_timer_and_outputs_off);
    RUN_TEST(test_holds_until_timer_expires_then_off);
    return UNITY_END();
}
