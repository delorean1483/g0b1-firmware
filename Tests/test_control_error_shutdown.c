#include "unity.h"
#include "control.h"

static apu_ctx_t ctx;
void setUp(void) { control_init(&ctx); ctx.op_state = OP_ERROR_SHUTDOWN; }
void tearDown(void) {}

static void set_all_outputs_on(apu_ctx_t *c) {
    c->out.fuel_pump = true; c->out.starter = true; c->out.glow_plug = true;
    c->out.compressor_clutch = true; c->out.evap_fan = true; c->cool_mode = true;
    c->out.condenser_fan = true; c->out.condenser_duty = 1000;
}

static void test_err_none_goes_off(void) {
    ctx.error_state = ERR_NONE;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);
}

static void test_low_oil_deenergizes(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_LOW_OIL;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    TEST_ASSERT_FALSE(ctx.out.condenser_fan);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);  /* latches */
}

static void test_high_engine_temp_deenergizes(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_HIGH_ENGINE_TEMP;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
}

static void test_low_battery_deenergizes(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_LOW_BATTERY;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.starter);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.engine_op_status);
}

static void test_starting_failure_deenergizes(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_STARTING_FAILURE;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.glow_plug);
}

static void test_no_rpm_deenergizes(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_NO_RPM_DETECTED;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
}

static void test_ac_low_pressure_kills_compressor_only(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_AC_LOW_PRESSURE;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_TRUE(ctx.out.fuel_pump);                    /* engine keeps running */
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state);
}

static void test_ac_high_pressure_kills_compressor_only(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_AC_HIGH_PRESSURE;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_TRUE(ctx.out.fuel_pump);
}

static void test_standby_recovers_when_truck_off(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_STANDBY;
    ctx.op_state_previous = OP_CLIMATE; ctx.in_truck_ignition = false; /* truck off */
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_NONE, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);
}

static void test_standby_recovers_on_override(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_STANDBY;
    ctx.op_state_previous = OP_BATTERY; ctx.in_truck_ignition = true; ctx.standby_override = true;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(ERR_NONE, ctx.error_state);
    TEST_ASSERT_EQUAL_INT(OP_BATTERY, ctx.op_state);
}

static void test_standby_latches_when_truck_on(void) {
    set_all_outputs_on(&ctx); ctx.error_state = ERR_STANDBY;
    ctx.op_state_previous = OP_CLIMATE; ctx.in_truck_ignition = true; ctx.standby_override = false;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);                   /* de-energized */
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, ctx.op_state); /* stays */
    TEST_ASSERT_EQUAL_UINT8(ERR_STANDBY, ctx.error_state);
}

static void test_engine_stalled_goes_off(void) {           /* code 8 -> default -> OFF */
    ctx.error_state = ERR_ENGINE_STALLED;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.sub_state);
}

static void test_high_ac_pressure_goes_off(void) {         /* code 10 -> default -> OFF */
    ctx.error_state = ERR_HIGH_AC_PRESSURE;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
}

static void test_sets_temp_display_realtime(void) {
    ctx.error_state = ERR_LOW_OIL; ctx.temp_display_state = TD_CC_SETTING;
    control_error_shutdown_mode(&ctx);
    TEST_ASSERT_EQUAL_UINT8(TD_REAL_TIME, ctx.temp_display_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_err_none_goes_off);
    RUN_TEST(test_low_oil_deenergizes);
    RUN_TEST(test_high_engine_temp_deenergizes);
    RUN_TEST(test_low_battery_deenergizes);
    RUN_TEST(test_starting_failure_deenergizes);
    RUN_TEST(test_no_rpm_deenergizes);
    RUN_TEST(test_ac_low_pressure_kills_compressor_only);
    RUN_TEST(test_ac_high_pressure_kills_compressor_only);
    RUN_TEST(test_standby_recovers_when_truck_off);
    RUN_TEST(test_standby_recovers_on_override);
    RUN_TEST(test_standby_latches_when_truck_on);
    RUN_TEST(test_engine_stalled_goes_off);
    RUN_TEST(test_high_ac_pressure_goes_off);
    RUN_TEST(test_sets_temp_display_realtime);
    return UNITY_END();
}
