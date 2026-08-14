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
    sched_register(SLOT_10MS,  control_10ms_slot);
    sched_register(SLOT_1MIN,  control_1min_slot);
}
void tearDown(void) {}

static void advance(uint32_t total_ms) {
    for (uint32_t t = 0; t < total_ms; t++) { sched_service(1); sched_run(); }
}

/* One minute of scheduler time fires SLOT_1MIN once, incrementing the machine minute counter.
   (60 min to bump the NVM hour is impractical to advance; the hour bump is unit-tested.) */
static void test_1min_slot_increments_machine_min(void) {
    apu_ctx_t *c = control_app_ctx();
    TEST_ASSERT_EQUAL_UINT8(0, c->machine_run_min);
    advance(60000);   /* 1 minute */
    TEST_ASSERT_EQUAL_UINT8(1, c->machine_run_min);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_1min_slot_increments_machine_min);
    return UNITY_END();
}
