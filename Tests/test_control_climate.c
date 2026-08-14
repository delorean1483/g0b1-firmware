#include "unity.h"
#include "control.h"
#include "app_timers.h"

static apu_ctx_t ctx;
void setUp(void) { app_timers_init(); control_init(&ctx); ctx.op_state = OP_CLIMATE; ctx.sub_state = 0; }
void tearDown(void) {}

static void test_settle_arms_1s_and_advances(void) {
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(TD_REAL_TIME, ctx.temp_display_state);
    TEST_ASSERT_EQUAL_UINT16(100, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR));
    TEST_ASSERT_EQUAL_UINT8(1, ctx.sub_state);          /* CC_START_ENGINE */
}

static void test_start_engine_hands_off_when_engine_off(void) {
    ctx.sub_state = 1 /*CC_START_ENGINE*/;
    ctx.out.fuel_pump = false;                          /* engine not running */
    /* SHORT_DELAY_TMR is 0 (expired) in a fresh app_timers_init */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(OP_CLIMATE, ctx.op_state_previous);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);          /* ES_GLOWPLUG_ON */
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
}

static void test_start_engine_skips_when_engine_running(void) {
    ctx.sub_state = 1; ctx.out.fuel_pump = true;        /* engine running */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* CC_MONITOR_TEMP */
}

static void test_monitor_temp_cool_call(void) {
    ctx.sub_state = 2; ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 73; /* >= 70+3 */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ST_COOLING, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(3, ctx.sub_state);          /* CC_START_COOL */
}

static void test_monitor_temp_chillin_stays(void) {
    ctx.sub_state = 2; ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 67; /* <= 70-3 */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ST_CHILLIN, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* stays MONITOR_TEMP */
}

static void test_monitor_temp_in_band_no_change(void) {
    ctx.sub_state = 2; ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 70; /* within +/-3 */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* stays */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_settle_arms_1s_and_advances);
    RUN_TEST(test_start_engine_hands_off_when_engine_off);
    RUN_TEST(test_start_engine_skips_when_engine_running);
    RUN_TEST(test_monitor_temp_cool_call);
    RUN_TEST(test_monitor_temp_chillin_stays);
    RUN_TEST(test_monitor_temp_in_band_no_change);
    return UNITY_END();
}
