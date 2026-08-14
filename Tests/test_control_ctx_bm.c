#include "unity.h"
#include "control.h"

static apu_ctx_t ctx;
void setUp(void) { control_init(&ctx); }
void tearDown(void) {}

static void test_err_codes_8_9_10(void) {
    TEST_ASSERT_EQUAL_INT(8, ERR_ENGINE_STALLED);
    TEST_ASSERT_EQUAL_INT(9, ERR_NO_RPM_DETECTED);
    TEST_ASSERT_EQUAL_INT(10, ERR_HIGH_AC_PRESSURE);
}

static void test_init_resets_bm_fields(void) {
    TEST_ASSERT_EQUAL_UINT8(0, ctx.attempted_charging_counter);
    TEST_ASSERT_EQUAL_UINT16(0, ctx.battery_voltage);
    TEST_ASSERT_EQUAL_UINT16(0, ctx.batt_monitor_setting);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_err_codes_8_9_10);
    RUN_TEST(test_init_resets_bm_fields);
    return UNITY_END();
}
