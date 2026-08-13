#include "unity.h"
#include "bsp_pwm.h"
#include "fan_speed.h"
#include "fake_bsp_pwm.h"

static bsp_pwm_backend_t be;
void setUp(void) { fake_bsp_pwm_init(&be); bsp_pwm_init(&be); }
void tearDown(void) {}

static void test_pwm_set_get_roundtrip(void) {
    bsp_pwm_set(PWM_EVAP_FAN, 545);
    TEST_ASSERT_EQUAL_UINT16(545, bsp_pwm_get(PWM_EVAP_FAN));
    TEST_ASSERT_EQUAL_UINT16(545, fake_bsp_pwm_duty(PWM_EVAP_FAN));
    TEST_ASSERT_EQUAL_UINT16(0, bsp_pwm_get(PWM_CONDENSER_FAN)); /* independent */
}

static void test_pwm_clamps_over_max(void) {
    bsp_pwm_set(PWM_EVAP_FAN, 1500);
    TEST_ASSERT_EQUAL_UINT16(BSP_PWM_MAX, bsp_pwm_get(PWM_EVAP_FAN));
}

static void test_fan_speed_duty_ratios(void) {
    TEST_ASSERT_EQUAL_UINT16(318,  fan_speed_permille(FAN_LOW));
    TEST_ASSERT_EQUAL_UINT16(545,  fan_speed_permille(FAN_MEDIUM));
    TEST_ASSERT_EQUAL_UINT16(1000, fan_speed_permille(FAN_HIGH));
}

static void test_fan_speed_applied_to_pwm(void) {
    bsp_pwm_set(PWM_EVAP_FAN, fan_speed_permille(FAN_LOW));
    TEST_ASSERT_EQUAL_UINT16(318, bsp_pwm_get(PWM_EVAP_FAN));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pwm_set_get_roundtrip);
    RUN_TEST(test_pwm_clamps_over_max);
    RUN_TEST(test_fan_speed_duty_ratios);
    RUN_TEST(test_fan_speed_applied_to_pwm);
    return UNITY_END();
}
