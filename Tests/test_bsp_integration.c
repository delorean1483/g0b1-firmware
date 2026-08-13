#include "unity.h"
#include "sched.h"
#include "app_timers.h"
#include "io_debounce.h"
#include "bsp_io.h"
#include "bsp_pwm.h"
#include "fan_speed.h"
#include "fake_bsp_io.h"
#include "fake_bsp_pwm.h"

static bsp_io_backend_t  io_be;
static bsp_pwm_backend_t pwm_be;
static discrete_input_t  oil;

/* A 10 ms slot handler: sample the (debounced) oil-pressure input and, once it
   reads CLOSED, energize the fuel pump and drive the evap fan at LOW. */
static void ctrl_10ms(void) {
    io_debounce_service(&oil, bsp_in_read(IN_OIL_PRESSURE) ? SWITCH_CLOSED : SWITCH_OPEN);
    if (io_debounce_state(&oil) == SWITCH_CLOSED) {
        bsp_out_set(OUT_FUEL_PUMP, true);
        bsp_pwm_set(PWM_EVAP_FAN, fan_speed_permille(FAN_LOW));
    }
}

void setUp(void) {
    fake_bsp_io_init(&io_be);   bsp_io_init(&io_be);
    fake_bsp_pwm_init(&pwm_be); bsp_pwm_init(&pwm_be);
    io_debounce_init(&oil, DEBOUNCE_TIME, SWITCH_OPEN);
    sched_init();
    sched_register(SLOT_10MS, ctrl_10ms);
}
void tearDown(void) {}

static void advance(uint16_t total_ms) {
    for (uint16_t t = 0; t < total_ms; t += 1u) { sched_service(1u); sched_run(); }
}

static void test_debounced_input_drives_outputs_through_scheduler(void) {
    /* Oil pressure low the whole time -> nothing energizes. */
    advance(200);
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_FUEL_PUMP));
    TEST_ASSERT_EQUAL_UINT16(0, fake_bsp_pwm_duty(PWM_EVAP_FAN));

    /* Now assert oil pressure; the 10 ms slot fires every 10 ms, so DEBOUNCE_TIME(10)
       consecutive samples = 100 ms until the debounced state commits and outputs latch. */
    fake_bsp_io_set_input(IN_OIL_PRESSURE, true);
    advance(90);                              /* 9 slot samples: not yet committed */
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_FUEL_PUMP));
    advance(20);                              /* crosses the 10th sample -> commit */
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_FUEL_PUMP));
    TEST_ASSERT_EQUAL_UINT16(318, fake_bsp_pwm_duty(PWM_EVAP_FAN)); /* FAN_LOW */
}

static void test_second_timer_expires_under_scheduler(void) {
    app_timer_set(SCALE_SECOND, POWER_UP_TMR, 2);
    advance(1000);
    TEST_ASSERT_FALSE(app_timer_expired(SCALE_SECOND, POWER_UP_TMR)); /* 1 left */
    advance(1000);
    TEST_ASSERT_TRUE(app_timer_expired(SCALE_SECOND, POWER_UP_TMR));  /* reached 0 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_debounced_input_drives_outputs_through_scheduler);
    RUN_TEST(test_second_timer_expires_under_scheduler);
    return UNITY_END();
}
