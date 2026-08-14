#include "unity.h"
#include "control.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fake_nor.h"

static nvm_backend_t nor;
static apu_ctx_t ctx;
void setUp(void) { fake_nor_init(&nor); nvm_init(&nor); sensors_init(VREF_CAL_DEFAULT, 0); control_init(&ctx); }
void tearDown(void) {}

static void test_deenergize_all_clears_outputs_and_status(void) {
    ctx.out.fuel_pump = true; ctx.out.starter = true; ctx.out.glow_plug = true;
    ctx.out.compressor_clutch = true; ctx.out.heat_reverse = true; ctx.out.evap_fan = true;
    ctx.cool_mode = true; ctx.engine_op_status = ST_RUNNING; ctx.control_status = ST_CHARGING;
    control_deenergize_all(&ctx);
    TEST_ASSERT_FALSE(ctx.out.fuel_pump);
    TEST_ASSERT_FALSE(ctx.out.starter);
    TEST_ASSERT_FALSE(ctx.out.glow_plug);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
    TEST_ASSERT_FALSE(ctx.out.heat_reverse);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    TEST_ASSERT_FALSE(ctx.cool_mode);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.engine_op_status);
    TEST_ASSERT_EQUAL_UINT8(ST_OFF, ctx.control_status);
}

static void test_sample_copies_battery_voltage(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2000);
    control_sample_sensors(&ctx);
    TEST_ASSERT_EQUAL_UINT16(sensors_get_batt_cv(), ctx.battery_voltage);
}

static void test_battery_sample_settings_reads_nvm(void) {
    nvm_write_word(EE_MONITOR_BATT_SETTING, 1180);
    control_battery_sample_settings(&ctx);
    TEST_ASSERT_EQUAL_UINT16(1180, ctx.batt_monitor_setting);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_deenergize_all_clears_outputs_and_status);
    RUN_TEST(test_sample_copies_battery_voltage);
    RUN_TEST(test_battery_sample_settings_reads_nvm);
    return UNITY_END();
}
