#include "unity.h"
#include "control.h"
#include "sched.h"
#include "app_timers.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fan_speed.h"
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

/* Engine already running: seed CC_MONITOR_TEMP + fuel on. Drive cabin hot via the enclosure
   sensor and setpoint via NVM. Because control_10ms_slot runs control_sample_sensors every
   tick (cabin temp) and control_1s_slot runs control_climate_sample_settings (setpoint) each
   second, cabin/setpoint MUST be driven through the sensor/NVM, not set on ctx directly. */
static void test_cool_cycle_runs_then_returns_to_monitor(void) {
    apu_ctx_t *c = control_app_ctx();
    nvm_write_word(EE_CLIMATE_TEMP_SETTING, 70);
    /* Cabin hot: drive enclosure sensor to a high degF (>= 70+3). Pick a raw count that
       interpolates hot; verify with sensors_get_encl_temp_f() >= 73 after seeding. */
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_ENCL, 3600);
    TEST_ASSERT_TRUE(sensors_get_encl_temp_f() >= 73);   /* guard: really hot */
    c->op_state = OP_CLIMATE;
    c->sub_state = 2;                 /* CC_MONITOR_TEMP */
    c->out.fuel_pump = true;          /* engine running */
    c->compressor_off_timer = 15;     /* satisfy the min-off guard immediately */

    advance(3000);                    /* MONITOR->START_COOL->COMP_ON->EVAP_ON->CTRL_RUNNING */
    TEST_ASSERT_TRUE(c->out.compressor_clutch);
    TEST_ASSERT_TRUE(c->out.evap_fan);
    TEST_ASSERT_TRUE(c->out.condenser_fan);              /* OI-2 stub follows compressor */
    TEST_ASSERT_EQUAL_UINT8(8, c->sub_state);            /* CC_CTRL_RUNNING */

    /* Now drive cabin cold (<= setpoint+1) so it reaches setpoint and shuts the compressor. */
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_ENCL, 700);
    TEST_ASSERT_TRUE(sensors_get_encl_temp_f() <= 71);
    advance(20000);                   /* CTRL_RUNNING->EVAP_OFF->(15s off)->MONITOR_TEMP */
    TEST_ASSERT_EQUAL_UINT8(2, c->sub_state);            /* back to CC_MONITOR_TEMP */
    TEST_ASSERT_FALSE(c->out.compressor_clutch);
}

/* Engine not running at entry: START_ENGINE hands off to OP_ENGINE_START. */
static void test_climate_entry_hands_off_to_engine_start(void) {
    apu_ctx_t *c = control_app_ctx();
    c->op_state = OP_CLIMATE; c->sub_state = 0; c->out.fuel_pump = false;
    advance(1200);                    /* settle 1s then START_ENGINE fires */
    TEST_ASSERT_EQUAL_INT(OP_ENGINE_START, c->op_state);
    TEST_ASSERT_EQUAL_UINT8(OP_CLIMATE, c->op_state_previous);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cool_cycle_runs_then_returns_to_monitor);
    RUN_TEST(test_climate_entry_hands_off_to_engine_start);
    return UNITY_END();
}
