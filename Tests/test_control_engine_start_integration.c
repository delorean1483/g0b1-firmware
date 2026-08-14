#include "unity.h"
#include "control.h"
#include "sched.h"
#include "app_timers.h"
#include "sensors.h"
#include "sensors_cal.h"
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
    sensors_init(VREF_CAL_DEFAULT, 0);
    sched_init();
    control_app_init();
    sched_register(SLOT_10MS, control_10ms_slot);
}
void tearDown(void) {}

static void advance(uint16_t total_ms) {
    for (uint16_t t = 0; t < total_ms; t++) { sched_service(1); sched_run(); }
}

/* Full engine start: hot engine (glow 0s) -> ~1s fuel hold -> 4s crank -> 10s pressure wait,
   oil pressure good (debounced) -> RUNNING -> hand back to CLIMATE. Total ~15 s.
   NOTE: control_10ms_slot runs control_inputs_service AND control_sample_sensors every tick,
   so the oil-pressure state and external temperature MUST be driven through the fakes/sensors,
   not set on the ctx directly (the slot overwrites them). */
static void test_full_start_to_climate_handoff(void) {
    apu_ctx_t *c = control_app_ctx();
    /* Hot external temp so the glow-plug window is 0 s. Drive the M3 ext-NTC sensor:
       a raw count of 239 (just above NTC_OVERMAX_CNT) interpolates to ~246 degF (>=122). */
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_EXT, 239);
    /* Oil pressure good: drive the fake input high; it debounces over 500 ms, well before the
       ~5 s point where CHECK_PRESSURE reads it. */
    fake_bsp_io_set_input(IN_OIL_PRESSURE, true);
    c->op_state = OP_ENGINE_START;
    c->op_state_previous = OP_CLIMATE;
    c->engine_temp_ok = true;             /* not overwritten by any slot step yet (sensor deferred) */
    c->sub_state = 0;

    advance(16000);   /* covers glow(0s) + 1s fuel hold + 4s crank + 10s pressure wait + handoff */
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, c->op_state);        /* handed back to climate */
    TEST_ASSERT_EQUAL_UINT8(ST_RUNNING, c->engine_op_status);
    TEST_ASSERT_EQUAL_UINT8(2, c->sub_state);             /* CC_MONITOR_TEMP entry */
}

static void test_standby_aborts_start(void) {
    apu_ctx_t *c = control_app_ctx();
    c->op_state = OP_ENGINE_START; c->sub_state = 0; c->standby_override = false;
    fake_bsp_io_set_input(IN_TRUCK_IGNITION, true);       /* truck engine running */
    /* Truck-ignition debounces over CONTROL_INPUT_DEBOUNCE_TIME (50) service calls; the 10 ms
       slot services it once per 10 ms, so ~500 ms + margin, then the standby tail fires. */
    advance(700);
    TEST_ASSERT_EQUAL_INT(OP_ERROR_SHUTDOWN, c->op_state);
    TEST_ASSERT_EQUAL_UINT8(ERR_STANDBY, c->error_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_start_to_climate_handoff);
    RUN_TEST(test_standby_aborts_start);
    return UNITY_END();
}
