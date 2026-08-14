#include "unity.h"
#include "control.h"
#include "app_timers.h"

static apu_ctx_t ctx;
void setUp(void) { app_timers_init(); control_init(&ctx); ctx.op_state = OP_BATTERY; ctx.sub_state = 0; }
void tearDown(void) {}

static void test_bm_start_deenergizes_and_advances(void) {
    ctx.out.fuel_pump = true; ctx.attempted_start_counter = 4; ctx.attempted_charging_counter = 2;
    control_battery_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_charging_counter);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.sub_state);          /* BM_BATT_MONITOR */
}

static void test_bm_monitor_low_voltage_arms_10s(void) {
    ctx.sub_state = 1; ctx.battery_voltage = 1150; ctx.batt_monitor_setting = 1200; /* low */
    control_battery_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT16(1000, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR));
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* BM_START_ENGINE */
}

static void test_bm_monitor_ok_voltage_stays(void) {
    ctx.sub_state = 1; ctx.battery_voltage = 1250; ctx.batt_monitor_setting = 1200; /* ok */
    ctx.attempted_charging_counter = 2;
    control_battery_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_charging_counter);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.sub_state);          /* stays */
}

static void test_bm_start_engine_recovers_within_10s(void) {
    ctx.sub_state = 2; ctx.battery_voltage = 1250; ctx.batt_monitor_setting = 1200; /* recovered */
    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 5);    /* timer still running */
    control_battery_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.sub_state);          /* back to BM_BATT_MONITOR */
}

static void test_bm_start_engine_hands_off_after_10s(void) {
    ctx.sub_state = 2; ctx.battery_voltage = 1150; ctx.batt_monitor_setting = 1200;
    ctx.attempted_charging_counter = 1;                 /* -> 2, <=3 */
    /* SHORT_DELAY_TMR == 0 (expired) in fresh app_timers_init */
    control_battery_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(OP_BATTERY, ctx.op_state_previous);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);          /* ES_GLOWPLUG_ON */
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
}

static void test_bm_start_engine_4th_attempt_errors(void) {
    ctx.sub_state = 2; ctx.battery_voltage = 1150; ctx.batt_monitor_setting = 1200;
    ctx.attempted_charging_counter = 3;                 /* ++ -> 4, > 3 */
    control_battery_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_charging_counter);
    TEST_ASSERT_EQUAL_UINT8(6, ctx.sub_state);          /* BM_ERROR_PROCESS */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bm_start_deenergizes_and_advances);
    RUN_TEST(test_bm_monitor_low_voltage_arms_10s);
    RUN_TEST(test_bm_monitor_ok_voltage_stays);
    RUN_TEST(test_bm_start_engine_recovers_within_10s);
    RUN_TEST(test_bm_start_engine_hands_off_after_10s);
    RUN_TEST(test_bm_start_engine_4th_attempt_errors);
    return UNITY_END();
}
