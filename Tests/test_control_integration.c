#include "unity.h"
#include "control.h"
#include "sched.h"
#include "app_timers.h"
#include "bsp_io.h"
#include "bsp_pwm.h"
#include "mb_regmodel.h"
#include "fake_bsp_io.h"
#include "fake_bsp_pwm.h"

static bsp_io_backend_t io_be;
static bsp_pwm_backend_t pwm_be;

void setUp(void) {
    fake_bsp_io_init(&io_be);   bsp_io_init(&io_be);
    fake_bsp_pwm_init(&pwm_be); bsp_pwm_init(&pwm_be);
    mb_reg_reset();
    sched_init();               /* also app_timers_init */
    control_app_init();
    sched_register(SLOT_10MS, control_10ms_slot);
}
void tearDown(void) {}

/* advance total_ms in 1 ms steps through the real scheduler. */
static void advance(uint16_t total_ms) {
    for (uint16_t t = 0; t < total_ms; t++) { sched_service(1); sched_run(); }
}

static void test_powerup_transitions_to_off_via_timer(void) {
    apu_ctx_t *c = control_app_ctx();
    TEST_ASSERT_EQUAL_INT(OP_POWER_UP, c->op_state);
    advance(10);                            /* first 10ms slot: sub 0 -> timer=1, sub=1 */
    TEST_ASSERT_EQUAL_INT(OP_POWER_UP, c->op_state);
    advance(1000);                          /* the 1 s POWER_UP_TMR expires */
    TEST_ASSERT_EQUAL_INT(OP_OFF, c->op_state);
}

static void test_op_mode_write_drives_off_to_climate(void) {
    advance(1100);                          /* settle into OFF */
    TEST_ASSERT_EQUAL_INT(OP_OFF, control_app_ctx()->op_state);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(10, MODE_CLIMATE)); /* op-mode = climate */
    advance(10);                            /* next slot applies the transition */
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, control_app_ctx()->op_state);     /* reached (climate handler is M6c) */
}

static void test_off_keeps_outputs_deenergized(void) {
    advance(1100);                          /* OFF */
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_FUEL_PUMP));
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_COMPRESSOR_CLUTCH));
    TEST_ASSERT_EQUAL_UINT16(0, fake_bsp_pwm_duty(PWM_EVAP_FAN));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_powerup_transitions_to_off_via_timer);
    RUN_TEST(test_op_mode_write_drives_off_to_climate);
    RUN_TEST(test_off_keeps_outputs_deenergized);
    return UNITY_END();
}
