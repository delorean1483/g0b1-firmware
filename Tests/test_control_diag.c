#include "unity.h"
#include "mb_regmodel.h"
#include "control.h"
#include "app_timers.h"
#include "board_pins.h"

static apu_ctx_t ctx;

/* Put ctx in the exact state Component Test entry requires. */
static void ctx_ready_off(void) {
    control_init(&ctx);
    app_timers_init();
    ctx.op_state = OP_OFF;
    ctx.engine_op_status = ST_OFF;
    ctx.out.fuel_pump = false;
    ctx.in_truck_ignition = false;
    ctx.standby_override = false;
}

void setUp(void) {
    mb_reg_reset();
    ctx_ready_off();
    control_diag_register(&ctx);
}
void tearDown(void) {}

static void test_enter_from_off_ok(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(49, 1));
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(49, &o));
    TEST_ASSERT_EQUAL_UINT16(1, o);
}

static void test_enter_refused_when_ignition_on(void) {
    ctx.in_truck_ignition = true;
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(49, 1));
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
}

static void test_enter_allowed_ignition_on_with_override(void) {
    ctx.in_truck_ignition = true;
    ctx.standby_override = true;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(49, 1));
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);
}

static void test_enter_refused_when_not_off(void) {
    ctx.op_state = OP_CLIMATE;
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(49, 1));
    TEST_ASSERT_EQUAL_INT(OP_CLIMATE, ctx.op_state);
}

static void test_exit_returns_to_off(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(49, 1));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(49, 0));
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    uint16_t o = 1;
    mb_reg_read(49, &o);
    TEST_ASSERT_EQUAL_UINT16(0, o);
}

static void test_bad_mode_value_illegal(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(49, 2));
}

static void test_out_refused_when_not_in_diag(void) {
    /* not entered */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(50, (OUT_CONDENSER_FAN << 8) | 1));
}

static void test_low_risk_energize_sets_output_and_status(void) {
    mb_reg_write(49, 1);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(50, (OUT_CONDENSER_FAN << 8) | 1));
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.condenser_fan);
    TEST_ASSERT_EQUAL_UINT16(1000, ctx.out.condenser_duty);
    uint16_t o = 0;
    mb_reg_read(41, &o);
    TEST_ASSERT_EQUAL_UINT16((1u << OUT_CONDENSER_FAN), o);
}

static void test_single_active_releases_previous(void) {
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_CONDENSER_FAN << 8) | 1);
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.condenser_fan);
    mb_reg_write(50, (OUT_HEAT_REVERSER << 8) | 1);
    control_diag_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.condenser_fan);
    TEST_ASSERT_TRUE(ctx.out.heat_reverse);
    uint16_t o = 0;
    mb_reg_read(41, &o);
    TEST_ASSERT_EQUAL_UINT16((1u << OUT_HEAT_REVERSER), o);
}

static void test_off_clears_active(void) {
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_EVAP_FAN << 8) | 1);
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.evap_fan);
    mb_reg_write(50, (OUT_EVAP_FAN << 8) | 0);
    control_diag_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.evap_fan);
    uint16_t o = 9;
    mb_reg_read(41, &o);
    TEST_ASSERT_EQUAL_UINT16(0, o);
}

static void test_index_out_of_range_illegal(void) {
    mb_reg_write(49, 1);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(50, (OUT_COUNT << 8) | 1));
}

static void test_engine_relay_refused_when_ignition_on(void) {
    mb_reg_write(49, 1);
    ctx.in_truck_ignition = true;   /* becomes true after entry */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(50, (OUT_STARTER << 8) | 1));
}

static void test_engine_relay_allowed_when_off(void) {
    mb_reg_write(49, 1);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(50, (OUT_STARTER << 8) | 1));
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.starter);
}

/* Helper: advance the second-scale timers by n seconds. */
static void tick_seconds(int n) { for (int i = 0; i < n; i++) app_timers_tick(SCALE_SECOND); }

static void test_inactivity_timeout_exits(void) {
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_CONDENSER_FAN << 8) | 1);
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.condenser_fan);
    tick_seconds(10);                 /* inactivity was 10 s */
    control_diag_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_FALSE(ctx.out.condenser_fan);
}

static void test_out_write_refreshes_inactivity(void) {
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_HEAT_REVERSER << 8) | 1);
    tick_seconds(9);
    mb_reg_write(50, (OUT_HEAT_REVERSER << 8) | 1);   /* heartbeat resets to 10 */
    tick_seconds(9);
    control_diag_mode(&ctx);
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);      /* still alive */
    TEST_ASSERT_TRUE(ctx.out.heat_reverse);
}

static void test_engine_pulse_cap_drops_relay_but_stays(void) {
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_STARTER << 8) | 1);          /* cap 4 s */
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.starter);
    tick_seconds(4);
    control_diag_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.starter);                /* dropped */
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);      /* mode still active */
}

static void test_engine_heartbeat_does_not_rearm_cap(void) {
    /* C1: repeating the SAME engine energize (a master heartbeat) must NOT
     * re-arm the engine pulse-cap timer -- only the rising edge should. */
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_STARTER << 8) | 1);          /* rising edge: cap armed at 4 s */
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.starter);
    tick_seconds(3);
    mb_reg_write(50, (OUT_STARTER << 8) | 1);          /* heartbeat: same index, must not rearm */
    tick_seconds(2);                                    /* total 5 s > original 4 s cap */
    control_diag_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.starter);                /* dropped at the original cap */
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);
}

static void test_engine_gate_rechecked_every_tick(void) {
    /* I2: if truck ignition turns on mid-pulse, the energized engine relay
     * must drop immediately, not ride out the cap timer. */
    mb_reg_write(49, 1);
    mb_reg_write(50, (OUT_STARTER << 8) | 1);
    control_diag_mode(&ctx);
    TEST_ASSERT_TRUE(ctx.out.starter);
    ctx.in_truck_ignition = true;
    control_diag_mode(&ctx);
    TEST_ASSERT_FALSE(ctx.out.starter);
    TEST_ASSERT_EQUAL_INT(OP_DIAG, ctx.op_state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_from_off_ok);
    RUN_TEST(test_enter_refused_when_ignition_on);
    RUN_TEST(test_enter_allowed_ignition_on_with_override);
    RUN_TEST(test_enter_refused_when_not_off);
    RUN_TEST(test_exit_returns_to_off);
    RUN_TEST(test_bad_mode_value_illegal);
    RUN_TEST(test_out_refused_when_not_in_diag);
    RUN_TEST(test_low_risk_energize_sets_output_and_status);
    RUN_TEST(test_single_active_releases_previous);
    RUN_TEST(test_off_clears_active);
    RUN_TEST(test_index_out_of_range_illegal);
    RUN_TEST(test_engine_relay_refused_when_ignition_on);
    RUN_TEST(test_engine_relay_allowed_when_off);
    RUN_TEST(test_inactivity_timeout_exits);
    RUN_TEST(test_out_write_refreshes_inactivity);
    RUN_TEST(test_engine_pulse_cap_drops_relay_but_stays);
    RUN_TEST(test_engine_heartbeat_does_not_rearm_cap);
    RUN_TEST(test_engine_gate_rechecked_every_tick);
    return UNITY_END();
}
