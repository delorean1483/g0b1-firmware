#include "unity.h"
#include "control.h"
#include "bsp_io.h"
#include "bsp_pwm.h"
#include "fan_speed.h"
#include "app_timers.h"
#include "fake_bsp_io.h"
#include "fake_bsp_pwm.h"

static apu_ctx_t ctx;
static bsp_io_backend_t io_be;
static bsp_pwm_backend_t pwm_be;

void setUp(void) {
    fake_bsp_io_init(&io_be);   bsp_io_init(&io_be);
    fake_bsp_pwm_init(&pwm_be); bsp_pwm_init(&pwm_be);
    control_init(&ctx);
}
void tearDown(void) {}

static void test_relays_map_one_to_one(void) {
    ctx.out.fuel_pump = true; ctx.out.starter = true; ctx.out.glow_plug = true;
    ctx.out.compressor_clutch = true;
    outputs_apply(&ctx);
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_FUEL_PUMP));
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_STARTER));
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_GLOW_PLUG));
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_COMPRESSOR_CLUTCH));
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_HEAT_REVERSER)); /* not requested */
}

static void test_heat_reverse_maps_to_heat_reverser(void) {
    ctx.out.heat_reverse = true;
    outputs_apply(&ctx);
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_HEAT_REVERSER));  /* OI-1 */
}

static void test_evap_fan_on_sets_relay_and_pwm_duty(void) {
    ctx.out.evap_fan = true; ctx.out.evap_speed = 50;   /* 50% */
    outputs_apply(&ctx);
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_EVAP_FAN));
    TEST_ASSERT_EQUAL_UINT16(655, fake_bsp_pwm_duty(PWM_EVAP_FAN)); /* 50% -> 655 permille */
}

static void test_evap_fan_off_zeroes_pwm(void) {
    ctx.out.evap_fan = true; ctx.out.evap_speed = 100; outputs_apply(&ctx);
    ctx.out.evap_fan = false; outputs_apply(&ctx);
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_EVAP_FAN));
    TEST_ASSERT_EQUAL_UINT16(0, fake_bsp_pwm_duty(PWM_EVAP_FAN));
}

static void test_condenser_fan_relay_and_duty(void) {
    ctx.out.condenser_fan = true; ctx.out.condenser_duty = 700;
    outputs_apply(&ctx);
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_CONDENSER_FAN));
    TEST_ASSERT_EQUAL_UINT16(700, fake_bsp_pwm_duty(PWM_CONDENSER_FAN));
}

static void test_evap_forced_on_timer_keeps_fan_on(void) {
    app_timers_init();
    app_timer_set(SCALE_SECOND, EVAP_FORCED_ON_TMR, 5);   /* forced-on window active */
    ctx.out.evap_fan = false;                              /* request says off... */
    ctx.out.evap_speed = 100;
    outputs_apply(&ctx);
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_EVAP_FAN));       /* ...but forced on */
    TEST_ASSERT_EQUAL_UINT16(fan_duty_permille(100), fake_bsp_pwm_duty(PWM_EVAP_FAN));
}

static void test_evap_off_when_timer_expired_and_not_requested(void) {
    app_timers_init();                                     /* all timers 0 */
    ctx.out.evap_fan = false;
    outputs_apply(&ctx);
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_EVAP_FAN));
    TEST_ASSERT_EQUAL_UINT16(0, fake_bsp_pwm_duty(PWM_EVAP_FAN));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_relays_map_one_to_one);
    RUN_TEST(test_heat_reverse_maps_to_heat_reverser);
    RUN_TEST(test_evap_fan_on_sets_relay_and_pwm_duty);
    RUN_TEST(test_evap_fan_off_zeroes_pwm);
    RUN_TEST(test_condenser_fan_relay_and_duty);
    RUN_TEST(test_evap_forced_on_timer_keeps_fan_on);
    RUN_TEST(test_evap_off_when_timer_expired_and_not_requested);
    return UNITY_END();
}
