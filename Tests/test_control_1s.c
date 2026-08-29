#include "unity.h"
#include "control.h"
#include "fan_speed.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fake_nor.h"

static nvm_backend_t nor;
static apu_ctx_t ctx;
void setUp(void) { fake_nor_init(&nor); nvm_init(&nor); control_init(&ctx); }
void tearDown(void) {}

static void test_compressor_off_timer_counts_up_when_off(void) {
    ctx.out.compressor_clutch = false;
    ctx.compressor_on_timer = 7;               /* should be reset to 0 */
    control_service_compressor_timers(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.compressor_on_timer);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.compressor_off_timer);
}

static void test_compressor_on_timer_counts_up_when_on(void) {
    ctx.out.compressor_clutch = true;
    ctx.compressor_off_timer = 9;              /* should be reset to 0 */
    control_service_compressor_timers(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.compressor_off_timer);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.compressor_on_timer);
}

static void test_compressor_timer_caps_at_255(void) {
    ctx.out.compressor_clutch = false;
    ctx.compressor_off_timer = 255;
    control_service_compressor_timers(&ctx);
    TEST_ASSERT_EQUAL_UINT8(255, ctx.compressor_off_timer);   /* no wrap */
}

static void test_sample_settings_reads_nvm(void) {
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 72);
    nvm_write_byte(EE_EVAP_FAN_SPEED, 60);      /* 60 % */
    control_climate_sample_settings(&ctx);
    TEST_ASSERT_EQUAL_INT16(72, ctx.clmt_temp_setting);
    TEST_ASSERT_EQUAL_UINT8(60, ctx.evap_fan_speed);
}

static void test_sample_settings_clamps_evap_speed(void) {
    nvm_write_byte(EE_EVAP_FAN_SPEED, 150);     /* out of range -> clamp to 100 % */
    control_climate_sample_settings(&ctx);
    TEST_ASSERT_EQUAL_UINT8(100, ctx.evap_fan_speed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_compressor_off_timer_counts_up_when_off);
    RUN_TEST(test_compressor_on_timer_counts_up_when_on);
    RUN_TEST(test_compressor_timer_caps_at_255);
    RUN_TEST(test_sample_settings_reads_nvm);
    RUN_TEST(test_sample_settings_clamps_evap_speed);
    return UNITY_END();
}
