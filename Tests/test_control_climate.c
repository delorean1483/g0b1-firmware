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

static void test_start_cool_sets_flags(void) {
    ctx.sub_state = 3 /*CC_START_COOL*/;
    control_climate_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.cool_mode);
    TEST_ASSERT_FALSE(ctx.out.heat_reverse);
    TEST_ASSERT_EQUAL_UINT8(5, ctx.sub_state);          /* CC_COMP_ON */
}

static void test_comp_on_after_off_guard(void) {
    ctx.sub_state = 5 /*CC_COMP_ON*/; ctx.compressor_off_timer = 15;
    control_climate_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_SECOND, COMP_EVAP_DELAY_TMR));
    TEST_ASSERT_EQUAL_UINT8(7, ctx.sub_state);          /* CC_EVAP_ON */
}

static void test_comp_on_waits_for_off_guard(void) {
    ctx.sub_state = 5; ctx.compressor_off_timer = 10;   /* < 15 */
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(5, ctx.sub_state);          /* stays */
}

static void test_evap_on_turns_on_and_arms_defrost(void) {
    ctx.sub_state = 7 /*CC_EVAP_ON*/; ctx.evap_fan_speed = FAN_MEDIUM;
    /* COMP_EVAP_DELAY_TMR is 0 (expired) in fresh app_timers_init */
    control_climate_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.evap_fan);
    TEST_ASSERT_EQUAL_UINT8(FAN_MEDIUM, ctx.out.evap_speed);
    TEST_ASSERT_EQUAL_UINT16(10, app_timer_get(SCALE_SECOND, EVAP_FORCED_ON_TMR));
    TEST_ASSERT_EQUAL_UINT16(30, app_timer_get(SCALE_MINUTE, DEFROST_CYCLE_TMR));
    TEST_ASSERT_EQUAL_UINT8(8, ctx.sub_state);          /* CC_CTRL_RUNNING */
}

static void test_ctrl_running_reaches_setpoint(void) {
    ctx.sub_state = 8 /*CC_CTRL_RUNNING*/; ctx.cool_mode = true;
    ctx.out.compressor_clutch = true;
    app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 5);  /* defrost timer running */
    ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 71; /* <= 70+1 */
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(TD_CC_SETTING, ctx.temp_display_state);
    TEST_ASSERT_EQUAL_UINT8(12, ctx.sub_state);         /* CC_EVAP_OFF */
}

static void test_evap_off_returns_to_monitor(void) {
    ctx.sub_state = 12 /*CC_EVAP_OFF*/; ctx.cool_mode = true; ctx.out.evap_fan = true;
    ctx.compressor_off_timer = 15;
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.cool_mode);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* CC_MONITOR_TEMP */
}

static void test_evap_off_early_cool_clear(void) {
    ctx.sub_state = 12; ctx.cool_mode = true; ctx.out.evap_fan = true;
    ctx.compressor_off_timer = 5;                        /* < 15 */
    ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 67; /* <= 70-3 */
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.cool_mode);                   /* cool cleared early */
    TEST_ASSERT_TRUE(ctx.out.evap_fan);                 /* evap still on until 15s guard */
    TEST_ASSERT_EQUAL_UINT8(12, ctx.sub_state);         /* stays */
}

static void test_ctrl_running_defrost_branch_starts_defrost(void) {
    ctx.sub_state = 8 /*CC_CTRL_RUNNING*/; ctx.cool_mode = true;
    ctx.out.compressor_clutch = true; ctx.out.evap_fan = true;
    /* DEFROST_CYCLE_TMR == 0 (expired) in fresh app_timers_init -> defrost branch */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ST_DEFROST, ctx.control_status);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_FALSE(ctx.cool_mode);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    TEST_ASSERT_EQUAL_UINT16(45, app_timer_get(SCALE_SECOND, EVENT_INTERVAL_TMR));
    TEST_ASSERT_EQUAL_UINT8(10, ctx.sub_state);   /* CC_COOL_DEFROST_END */
}

static void test_cool_defrost_end_resumes(void) {
    ctx.sub_state = 10 /*CC_COOL_DEFROST_END*/;
    /* EVENT_INTERVAL_TMR is 0 (expired) in fresh app_timers_init */
    control_climate_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.compressor_clutch);
    TEST_ASSERT_TRUE(ctx.cool_mode);
    TEST_ASSERT_TRUE(ctx.out.evap_fan);
    TEST_ASSERT_EQUAL_UINT8(ST_COOLING, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT16(30, app_timer_get(SCALE_MINUTE, DEFROST_CYCLE_TMR));
    TEST_ASSERT_EQUAL_UINT8(8, ctx.sub_state);          /* CC_CTRL_RUNNING */
}

static void test_low_pressure_rechk_retries(void) {
    ctx.sub_state = 6 /*CC_AC_LOW_PRESSURE_RECHK*/; ctx.refregerant_check_counter = 3;
    ctx.out.compressor_clutch = true;
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(4, ctx.refregerant_check_counter);
    TEST_ASSERT_EQUAL_UINT8(5, ctx.sub_state);          /* CC_COMP_ON retry */
}

static void test_low_pressure_rechk_fails_after_10(void) {
    ctx.sub_state = 6; ctx.refregerant_check_counter = 10; /* ++ -> 11 > 10 */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.refregerant_check_counter);
    TEST_ASSERT_EQUAL_UINT8(13, ctx.sub_state);         /* CC_AC_LOW_PRESSURE_FAIL */
}

static void test_low_pressure_fail_shuts_down(void) {
    ctx.sub_state = 13 /*CC_AC_LOW_PRESSURE_FAIL*/;
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_AC_LOW_PRESSURE, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(OP_CLIMATE, ctx.op_state_previous);
}

static void test_high_pressure_rechk_waits(void) {
    ctx.sub_state = 17 /*CC_AC_HIGH_PRESSURE_RECHK*/; ctx.out.compressor_clutch = true;
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(18, ctx.sub_state);         /* CC_WAIT_HIGH_PRESSURE_NORMAL */
}

static void test_high_pressure_wait_resumes_when_ok(void) {
    ctx.sub_state = 18 /*CC_WAIT_HIGH_PRESSURE_NORMAL*/; ctx.ac_high_pressure_ok = true;
    control_climate_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_SECOND, COMP_EVAP_DELAY_TMR));
    TEST_ASSERT_EQUAL_UINT8(7, ctx.sub_state);          /* CC_EVAP_ON */
}

static void test_high_pressure_fail_shuts_down(void) {
    ctx.sub_state = 14 /*CC_AC_HIGH_PRESSURE_FAIL*/;
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_AC_HIGH_PRESSURE, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
}

static void test_tail_low_pressure_trips_rechk(void) {
    ctx.sub_state = 8 /*CC_CTRL_RUNNING*/; ctx.cool_mode = true;
    app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 5);  /* running, not defrost */
    ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 80; /* above setpoint+1, no cool-off */
    ctx.out.compressor_clutch = true; ctx.compressor_on_timer = 2; /* armed */
    ctx.ac_low_pressure_ok = false;
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(6, ctx.sub_state);          /* CC_AC_LOW_PRESSURE_RECHK */
}

static void test_tail_high_pressure_trips_rechk(void) {
    ctx.sub_state = 8; ctx.cool_mode = true;
    app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 5);
    ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 80;
    ctx.out.compressor_clutch = true; ctx.compressor_on_timer = 2;
    ctx.ac_low_pressure_ok = true; ctx.ac_high_pressure_ok = false;
    control_climate_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_EQUAL_UINT8(17, ctx.sub_state);         /* CC_AC_HIGH_PRESSURE_RECHK */
}

static void test_tail_normal_pressure_clears_counter(void) {
    ctx.sub_state = 8; ctx.cool_mode = true;
    app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 5);
    ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 80;
    ctx.out.compressor_clutch = true; ctx.compressor_on_timer = 2;
    ctx.refregerant_check_counter = 4;                  /* should clear */
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.refregerant_check_counter);
    TEST_ASSERT_TRUE(ctx.out.compressor_clutch);        /* stays on */
}

static void test_tail_engine_over_temp_shuts_down(void) {
    ctx.sub_state = 2 /*CC_MONITOR_TEMP*/; ctx.clmt_temp_setting = 70; ctx.cabin_temperature = 70;
    ctx.engine_temp_ok = false;
    control_climate_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_HIGH_ENGINE_TEMP, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_settle_arms_1s_and_advances);
    RUN_TEST(test_start_engine_hands_off_when_engine_off);
    RUN_TEST(test_start_engine_skips_when_engine_running);
    RUN_TEST(test_monitor_temp_cool_call);
    RUN_TEST(test_monitor_temp_chillin_stays);
    RUN_TEST(test_monitor_temp_in_band_no_change);
    RUN_TEST(test_start_cool_sets_flags);
    RUN_TEST(test_comp_on_after_off_guard);
    RUN_TEST(test_comp_on_waits_for_off_guard);
    RUN_TEST(test_evap_on_turns_on_and_arms_defrost);
    RUN_TEST(test_ctrl_running_reaches_setpoint);
    RUN_TEST(test_evap_off_returns_to_monitor);
    RUN_TEST(test_evap_off_early_cool_clear);
    RUN_TEST(test_ctrl_running_defrost_branch_starts_defrost);
    RUN_TEST(test_cool_defrost_end_resumes);
    RUN_TEST(test_low_pressure_rechk_retries);
    RUN_TEST(test_low_pressure_rechk_fails_after_10);
    RUN_TEST(test_low_pressure_fail_shuts_down);
    RUN_TEST(test_high_pressure_rechk_waits);
    RUN_TEST(test_high_pressure_wait_resumes_when_ok);
    RUN_TEST(test_high_pressure_fail_shuts_down);
    RUN_TEST(test_tail_low_pressure_trips_rechk);
    RUN_TEST(test_tail_high_pressure_trips_rechk);
    RUN_TEST(test_tail_normal_pressure_clears_counter);
    RUN_TEST(test_tail_engine_over_temp_shuts_down);
    return UNITY_END();
}
