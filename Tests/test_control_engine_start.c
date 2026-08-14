#include "unity.h"
#include "control.h"
#include "app_timers.h"
#include "sensors.h"

static apu_ctx_t ctx;
void setUp(void) { app_timers_init(); control_init(&ctx); ctx.op_state = OP_ENGINE_START; ctx.sub_state = 0; }
void tearDown(void) {}

static void test_glow_duration_by_temp(void) {
    ctx.ext_temp_sensor_state = SENSOR_ON; ctx.external_temperature = 104;  /* 8s branch */
    control_engine_start_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.glow_plug);
    TEST_ASSERT_EQUAL_UINT8(ST_WARMING_UP, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT16(80, app_timer_get(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR));
    TEST_ASSERT_EQUAL_UINT8(1, ctx.attempted_start_counter);
    TEST_ASSERT_EQUAL_UINT8(2, ctx.sub_state);          /* ES_FUEL_ON */
}

static void test_glow_no_sensor_28s(void) {
    ctx.ext_temp_sensor_state = SENSOR_OFF;
    control_engine_start_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT16(280, app_timer_get(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR));
}

static void test_glow_hot_zero_and_off(void) {
    ctx.ext_temp_sensor_state = SENSOR_ON; ctx.external_temperature = 130;  /* >=122 */
    control_engine_start_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT16(0, app_timer_get(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR));
    TEST_ASSERT_FALSE(ctx.out.glow_plug);
}

static void test_fuel_on_after_glow(void) {
    ctx.sub_state = 2 /*ES_FUEL_ON*/;
    /* glow timer 0, short-delay 0 -> fuel on */
    control_engine_start_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.fuel_pump);
    TEST_ASSERT_FALSE(ctx.out.glow_plug);
    TEST_ASSERT_EQUAL_UINT16(100, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR)); /* 1s */
    TEST_ASSERT_EQUAL_UINT8(3, ctx.sub_state);          /* ES_STARTER_ON */
}

static void test_starter_on_after_fuel(void) {
    ctx.sub_state = 3 /*ES_STARTER_ON*/;                /* short-delay 0 */
    control_engine_start_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.starter);
    TEST_ASSERT_EQUAL_UINT8(ST_STARTING, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT16(400, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR)); /* 4s */
    TEST_ASSERT_EQUAL_UINT8(4, ctx.sub_state);          /* ES_ENGINE_ON */
}

static void test_engine_on_stops_starter_and_postheats(void) {
    ctx.sub_state = 4 /*ES_ENGINE_ON*/; ctx.external_temperature = 100; /* <122 -> post-heat */
    ctx.out.starter = true;
    control_engine_start_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.starter);
    TEST_ASSERT_TRUE(ctx.out.glow_plug);                /* post-heat */
    TEST_ASSERT_EQUAL_UINT16(50, app_timer_get(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR)); /* 5s */
    TEST_ASSERT_EQUAL_UINT16(1000, app_timer_get(SCALE_TEN_MS, SHORT_DELAY_TMR));    /* 10s */
    TEST_ASSERT_EQUAL_UINT8(5, ctx.sub_state);          /* ES_CHECK_PRESSURE */
}

static void test_check_pressure_oil_ok_runs(void) {
    ctx.sub_state = 5 /*ES_CHECK_PRESSURE*/;
    ctx.in_oil_pressure_ok = true;                     /* pressure good */
    ctx.attempted_start_counter = 3;
    /* SHORT_DELAY_TMR is 0 (expired) in a fresh app_timers_init */
    control_engine_start_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ST_RUNNING, ctx.engine_op_status);
    TEST_ASSERT_EQUAL_UINT8(ST_RUNNING, ctx.control_status);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
    TEST_ASSERT_EQUAL_UINT8(6, ctx.sub_state);         /* ES_COOL_ON */
}

static void test_check_pressure_oil_low_retry(void) {
    ctx.sub_state = 5; ctx.in_oil_pressure_ok = false; ctx.attempted_start_counter = 2;
    ctx.out.fuel_pump = true;
    control_engine_start_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);         /* ES_GLOWPLUG_ON retry */
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, ctx.op_state); /* still starting */
}

static void test_check_pressure_oil_low_5th_attempt_fails(void) {
    ctx.sub_state = 5; ctx.in_oil_pressure_ok = false; ctx.attempted_start_counter = 5;
    control_engine_start_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_STARTING_FAILURE, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_start_counter);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_glow_duration_by_temp);
    RUN_TEST(test_glow_no_sensor_28s);
    RUN_TEST(test_glow_hot_zero_and_off);
    RUN_TEST(test_fuel_on_after_glow);
    RUN_TEST(test_starter_on_after_fuel);
    RUN_TEST(test_engine_on_stops_starter_and_postheats);
    RUN_TEST(test_check_pressure_oil_ok_runs);
    RUN_TEST(test_check_pressure_oil_low_retry);
    RUN_TEST(test_check_pressure_oil_low_5th_attempt_fails);
    return UNITY_END();
}
