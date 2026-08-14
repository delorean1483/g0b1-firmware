#include "unity.h"
#include "control.h"
#include "app_timers.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fake_nor.h"

static nvm_backend_t nor;
static apu_ctx_t ctx;
void setUp(void) { fake_nor_init(&nor); nvm_init(&nor); app_timers_init(); control_init(&ctx); }
void tearDown(void) {}

static void test_oil_good_below_500(void) {
    nvm_write_word(ENGINE_OILTIME_START, 499);
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_GOOD, ctx.oil_change_state);
}

static void test_oil_soon_in_500_580(void) {
    nvm_write_word(ENGINE_OILTIME_START, 500);   /* timer 0 (fresh init) */
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_CHANGE_SOON, ctx.oil_change_state);
}

static void test_oil_needed_in_580_700(void) {
    nvm_write_word(ENGINE_OILTIME_START, 580);
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_CHANGE_NEEDED, ctx.oil_change_state);
}

static void test_oil_past_due_at_700(void) {
    nvm_write_word(ENGINE_OILTIME_START, 700);
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_CHANGE_PAST_DUE, ctx.oil_change_state);
}

static void test_oil_timer_running_holds_state(void) {
    nvm_write_word(ENGINE_OILTIME_START, 600);
    app_timer_set(SCALE_MINUTE, NEXT_OIL_WARNING_TMR, 100);   /* not expired */
    ctx.oil_change_state = OIL_GOOD;
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_GOOD, ctx.oil_change_state);   /* no change while timer runs */
}

static void test_oil_dismissed_reloads_timer_soon(void) {
    nvm_write_word(ENGINE_OILTIME_START, 550);
    ctx.oil_change_state = OIL_WARNING_DISMISSED;             /* timer 0 */
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_WARNING_DISMISSED, ctx.oil_change_state); /* stays dismissed */
    TEST_ASSERT_EQUAL_UINT16(1200, app_timer_get(SCALE_MINUTE, NEXT_OIL_WARNING_TMR));
}

static void test_oil_dismissed_reloads_timer_past_due(void) {
    nvm_write_word(ENGINE_OILTIME_START, 750);
    ctx.oil_change_state = OIL_WARNING_DISMISSED;
    control_oil_change_check(&ctx);
    TEST_ASSERT_EQUAL_UINT8(OIL_WARNING_DISMISSED, ctx.oil_change_state);
    TEST_ASSERT_EQUAL_UINT16(300, app_timer_get(SCALE_MINUTE, NEXT_OIL_WARNING_TMR));
}

static void test_machine_accumulates_without_fuel(void) {
    ctx.out.fuel_pump = false; ctx.machine_run_min = 59;
    nvm_write_word(MACHINE_RUNTIME_START, 10);
    control_service_runtime(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.machine_run_min);                  /* rolled over */
    TEST_ASSERT_EQUAL_UINT16(11, nvm_read_word(MACHINE_RUNTIME_START));
    TEST_ASSERT_EQUAL_UINT8(0, ctx.engine_run_min);                   /* engine untouched (no fuel) */
}

static void test_engine_and_oil_accumulate_with_fuel(void) {
    ctx.out.fuel_pump = true; ctx.engine_run_min = 59; ctx.engine_oil_min = 59;
    nvm_write_word(ENGINE_RUNTIME_START, 20);
    nvm_write_word(ENGINE_OILTIME_START, 100);                        /* < 500 -> OIL_GOOD after check */
    control_service_runtime(&ctx);
    TEST_ASSERT_EQUAL_UINT16(21, nvm_read_word(ENGINE_RUNTIME_START));
    TEST_ASSERT_EQUAL_UINT16(101, nvm_read_word(ENGINE_OILTIME_START));
    TEST_ASSERT_EQUAL_UINT8(0, ctx.engine_oil_min);
    TEST_ASSERT_EQUAL_UINT8(OIL_GOOD, ctx.oil_change_state);          /* oil check ran */
}

static void test_engine_min_increments_no_rollover(void) {
    ctx.out.fuel_pump = true; ctx.engine_run_min = 5;
    nvm_write_word(ENGINE_RUNTIME_START, 20);
    control_service_runtime(&ctx);
    TEST_ASSERT_EQUAL_UINT8(6, ctx.engine_run_min);
    TEST_ASSERT_EQUAL_UINT16(20, nvm_read_word(ENGINE_RUNTIME_START)); /* no bump yet */
}

static void test_hour_bump_saturates_at_65535(void) {
    ctx.machine_run_min = 59;
    nvm_write_word(MACHINE_RUNTIME_START, 65535);
    control_service_runtime(&ctx);
    TEST_ASSERT_EQUAL_UINT16(65535, nvm_read_word(MACHINE_RUNTIME_START)); /* no wrap */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_oil_good_below_500);
    RUN_TEST(test_oil_soon_in_500_580);
    RUN_TEST(test_oil_needed_in_580_700);
    RUN_TEST(test_oil_past_due_at_700);
    RUN_TEST(test_oil_timer_running_holds_state);
    RUN_TEST(test_oil_dismissed_reloads_timer_soon);
    RUN_TEST(test_oil_dismissed_reloads_timer_past_due);
    RUN_TEST(test_machine_accumulates_without_fuel);
    RUN_TEST(test_engine_and_oil_accumulate_with_fuel);
    RUN_TEST(test_engine_min_increments_no_rollover);
    RUN_TEST(test_hour_bump_saturates_at_65535);
    return UNITY_END();
}
