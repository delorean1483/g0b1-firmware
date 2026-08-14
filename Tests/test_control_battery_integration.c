#include "unity.h"
#include "control.h"
#include "sched.h"
#include "app_timers.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "nvm.h"
#include "nvm_map.h"
#include "bsp_io.h"
#include "bsp_pwm.h"
#include "mb_regmodel.h"
#include "fake_bsp_io.h"
#include "fake_bsp_pwm.h"
#include "fake_nor.h"

static bsp_io_backend_t io_be;
static bsp_pwm_backend_t pwm_be;
static nvm_backend_t nor;

void setUp(void) {
    fake_bsp_io_init(&io_be);   bsp_io_init(&io_be);
    fake_bsp_pwm_init(&pwm_be); bsp_pwm_init(&pwm_be);
    mb_reg_reset();
    sensors_init(VREF_CAL_DEFAULT, 0);
    fake_nor_init(&nor); nvm_init(&nor);
    sched_init();
    control_app_init();
    sched_register(SLOT_10MS, control_10ms_slot);
    sched_register(SLOT_1S,   control_1s_slot);
}
void tearDown(void) {}

static void advance(uint32_t total_ms) {
    for (uint32_t t = 0; t < total_ms; t++) { sched_service(1); sched_run(); }
}

/* Battery monitor: setpoint high, measured battery low -> after 10 s confirm, hand off to
   engine-start with op_state_previous = OP_BATTERY. Drive battery via the M3 sensor and the
   setpoint via NVM (the slots overwrite the ctx copies each tick), not by poking ctx. */
static void test_low_battery_hands_off_to_engine_start(void) {
    apu_ctx_t *c = control_app_ctx();
    nvm_write_word(EE_MONITOR_BATT_SETTING, 1300);           /* setpoint 13.0 V */
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 1000); /* low battery */
    TEST_ASSERT_TRUE(sensors_get_batt_cv() < 1300);          /* guard: really low */
    c->op_state = OP_BATTERY; c->sub_state = 0;              /* BM_START */

    advance(11100);   /* START -> MONITOR (batt_monitor_setting refreshes at t=1s) -> arm 10 s ->
                          START_ENGINE -> handoff at t=11010 ms; 11100 gives margin */
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, c->op_state);
    TEST_ASSERT_EQUAL_UINT8(OP_BATTERY, c->op_state_previous);
}

/* Fault injection: seed an error + OP_ERROR_SHUTDOWN, run the slot, outputs de-energize. */
static void test_error_shutdown_deenergizes(void) {
    apu_ctx_t *c = control_app_ctx();
    c->op_state = OP_ERROR_SHUTDOWN; c->error_state = ERR_HIGH_ENGINE_TEMP;
    c->out.fuel_pump = true; c->out.compressor_clutch = true;
    advance(50);
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_FUEL_PUMP));
    TEST_ASSERT_FALSE(fake_bsp_io_out(OUT_COMPRESSOR_CLUTCH));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_low_battery_hands_off_to_engine_start);
    RUN_TEST(test_error_shutdown_deenergizes);
    return UNITY_END();
}
