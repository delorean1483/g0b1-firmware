#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const regs3[] = { &fake_drv[0], &fake_drv[1], &fake_drv[2] };
static htr_fsm_cfg_t cfg;
static uint32_t now;

static void tick_to(uint32_t t){ while((int32_t)(t - now) > 0){ htr_fsm_process(now); now += 10u; } htr_fsm_process(now); }
static htr_fsm_view_t view(void){ htr_fsm_view_t v; htr_fsm_view(now,&v); return v; }
static void req(uint8_t run){ htr_request_t r={run,5,22,HTR_AMBIENT_UNKNOWN}; htr_fsm_set_request(&r); }

void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4);
    cfg.drivers = regs3; cfg.n_drivers = 3; cfg.gpio = &g.api; cfg.pwr_for_3w = false;
    cfg.uart_3w = &u3.api; cfg.uart_4w = &u4.api; now = 0;
}
void tearDown(void){}

static void boot_auto(uint8_t pct_a, uint8_t pct_b){
    g.high_pct[HTR_WIRING_3W]=pct_a; g.high_pct[HTR_WIRING_4W]=pct_b;
    htr_fsm_init(&cfg, HTR_TYPE_AUTO, now);
}

void test_boot_is_tx_quiet_and_iface_off_for_3s(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
    tick_to(2990);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, view().phase);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].inits + fake_ctx[1].inits);
    TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_EN]); TEST_ASSERT_FALSE(g.line[HTR_LINE_TX_PWR]);
    TEST_ASSERT_EQUAL(0u, g.reads[HTR_WIRING_3W]);              /* not even sensing */
}
void test_3w_signature_probes_only_3w_drivers_in_order(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_ABSENT; fake_ctx[2].probe_answer=HTR_PROBE_FOUND;
    tick_to(6000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_EQUAL_UINT8(3u, v.active_type);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_FOUND, v.detect_result);
    TEST_ASSERT_EQUAL_HEX8(HTR_LS_A_IDLE_HIGH | (HTR_LS_CLASS_3W<<HTR_LS_CLASS_SHIFT), v.line_sense);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].inits);                 /* 4W driver never touched */
    TEST_ASSERT_TRUE(fake_ctx[0].probe_calls > 0);
    TEST_ASSERT_TRUE(fake_ctx[2].uart == &u3.api);
    TEST_ASSERT_TRUE(v.flags & HTR_F_DETECT_DONE); TEST_ASSERT_TRUE(v.flags & HTR_F_PRESENT);
}
void test_4w_signature_selects_4w_driver_and_powers_4w_path(void){
    boot_auto(100,100); fake_ctx[1].probe_answer=HTR_PROBE_FOUND;
    tick_to(6000);
    TEST_ASSERT_EQUAL_UINT8(2u, view().active_type);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].inits + fake_ctx[2].inits);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_MODE]); TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_PWR]);
    TEST_ASSERT_TRUE(fake_ctx[1].uart == &u4.api);
}
void test_push_pull_4w_never_enabled_without_4w_signature(void){
    boot_auto(50,40);      /* ambiguous lines */
    fake_ctx[1].probe_answer=HTR_PROBE_FOUND;     /* a 4W driver would answer if asked */
    tick_to(30000);
    for(size_t i=0;i<g.n;i++)
        if(g.ev[i].kind==EV_LINE && g.ev[i].a==HTR_LINE_TX_MODE) TEST_ASSERT_EQUAL_UINT8(0,g.ev[i].b);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].inits);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_LS_CLASS_AMBIG, v.line_sense >> HTR_LS_CLASS_SHIFT);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_UNKNOWN, v.detect_result);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, v.phase);
}
void test_no_heater_retries_every_10s(void){
    boot_auto(0,0); tick_to(5000);
    TEST_ASSERT_EQUAL_UINT8(HTR_DET_NO_HEATER, view().detect_result);
    uint32_t r1=g.reads[HTR_WIRING_3W]; tick_to(14000);
    TEST_ASSERT_EQUAL(r1, g.reads[HTR_WIRING_3W]);          /* backing off */
    tick_to(16000);
    TEST_ASSERT_TRUE(g.reads[HTR_WIRING_3W] > r1);          /* sensing again */
    g.high_pct[HTR_WIRING_3W]=100; fake_ctx[0].probe_answer=HTR_PROBE_FOUND; tick_to(30000);
    TEST_ASSERT_EQUAL_UINT8(1u, view().active_type);        /* hot-plugged */
}
static void boot_found_3w(void){
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND; tick_to(6000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
}
void test_full_cycle_start_run_stop_cooldown_off(void){
    boot_found_3w();
    req(1); tick_to(6100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts); TEST_ASSERT_EQUAL_UINT8(5, fake_ctx[0].last_rq.level);
    fake_ctx[0].hint=HTR_HINT_STARTING; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, view().phase);
    fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);
    req(0); tick_to(8100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(9000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, view().phase);
    TEST_ASSERT_FALSE(view().flags & HTR_F_SAFE_OFF);
    fake_ctx[0].hint=HTR_HINT_OFF; tick_to(10000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_SAFE_OFF);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);               /* interface never dropped */
}
void test_start_timeout_faults_and_stops_without_cutting_power(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_STARTING;
    tick_to(6000 + HTR_START_TIMEOUT_MS + 100);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_FAULT_LATCHED);
    /* stop on entry, then re-stopped (rate-limited) while it still reports starting */
    TEST_ASSERT_GREATER_OR_EQUAL(1, (int)fake_ctx[0].stops);
    TEST_ASSERT_LESS_OR_EQUAL(2, (int)fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(0, v.rq.run);
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);
}
void test_no_autonomous_restart_after_fault(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_FAULT; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    req(1); tick_to(9000);                                  /* G0B1 keeps asserting run */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_clear_fault(now)); /* heater not in standby */
    fake_ctx[0].hint=HTR_HINT_OFF; tick_to(9500);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_OK, htr_fsm_clear_fault(now));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);              /* still needs a new request */
    req(1); tick_to(9600);
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
void test_unexpected_heater_stop_while_running_is_a_fault(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
}
void test_comms_loss_is_flag_only(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].alive=false; tick_to(7000 + 60000);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, v.phase);
    TEST_ASSERT_TRUE(v.flags & HTR_F_COMMS_FAULT);
    TEST_ASSERT_FALSE(v.flags & HTR_F_FRESH);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
}
void test_cooldown_timeout_faults_but_keeps_polling(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    req(0); fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(8000);
    unsigned p=fake_ctx[0].processes;
    tick_to(8000 + HTR_COOLDOWN_TIMEOUT_MS + 100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    tick_to(now + 1000);
    TEST_ASSERT_TRUE(fake_ctx[0].processes > p + 100);
}
void test_live_level_change_applies_to_driver(void){
    boot_found_3w(); req(1); tick_to(6100);
    unsigned a0 = fake_ctx[0].applies;
    htr_request_t r={1,9,22,HTR_AMBIENT_UNKNOWN}; htr_fsm_set_request(&r);
    TEST_ASSERT_EQUAL(a0 + 1u, fake_ctx[0].applies); TEST_ASSERT_EQUAL_UINT8(9, fake_ctx[0].last_rq.level);
    htr_fsm_set_request(&r);                                /* idempotent rewrite */
    TEST_ASSERT_EQUAL(a0 + 1u, fake_ctx[0].applies);
}
void test_hotswap_redetects_only_while_off(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    fake_ctx[0].alive=false; tick_to(7000 + HTR_HOTSWAP_MS + 5000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);  /* never re-probe a maybe-running heater */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].inits);
    setUp(); boot_found_3w(); fake_ctx[0].alive=false;
    tick_to(6000 + HTR_FRESH_MS + HTR_HOTSWAP_MS + 100);   /* stale = past the fresh window */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, view().phase);
}
void test_forced_type_skips_sense_and_probe(void){
    g.high_pct[HTR_WIRING_3W]=0; g.high_pct[HTR_WIRING_4W]=0;
    htr_fsm_init(&cfg, 2u, now); tick_to(3500);
    htr_fsm_view_t v=view();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, v.phase);
    TEST_ASSERT_EQUAL_UINT8(2u, v.active_type);
    TEST_ASSERT_EQUAL(0u, fake_ctx[1].probe_calls);
    TEST_ASSERT_EQUAL(0u, g.reads[HTR_WIRING_3W] + g.reads[HTR_WIRING_4W]);
}
void test_type_change_rejected_while_running(void){
    boot_found_3w(); req(1); tick_to(6100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_set_type(2u, now));
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_redetect(now));
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_SUPPORTED, htr_fsm_set_type(9u, now));
}
/* HAL tick wraps after ~49.7 days: every timeout must use unsigned deltas. */
void test_full_cycle_across_tick_wrap(void){
    now = 0xFFFFF000u;                                /* ~4 s before wrap */
    boot_auto(100,0); fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
    tick_to(0xFFFFF000u + 6000u);                    /* crosses 0 during detection */
    TEST_ASSERT_TRUE(now < 0x10000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
    req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(now + 1000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, view().phase);
    TEST_ASSERT_TRUE(view().flags & HTR_F_FRESH);
}
/* Review finding 1: a heater that is running/starting while nothing asks for
   heat (coprocessor reset mid-run, or a start processed after a no-op stop)
   must be told to stop, never left running under PHASE OFF. */
void test_off_with_heater_reporting_running_sends_stop(void){
    boot_found_3w(); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(6500);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, view().phase);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, view().phase);
}
/* Finding 1: a standby report taken right after the stop may predate it. */
void test_stop_requested_needs_a_settled_standby_report(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_RUNNING; tick_to(7000);
    req(0); fake_ctx[0].hint=HTR_HINT_OFF; tick_to(7000 + 500);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, view().phase);
    tick_to(7000 + HTR_STOP_SETTLE_MS + 100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, view().phase);
}
/* Finding 2: changing type must not be a way out of FAULT. */
void test_type_change_rejected_in_fault(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_FAULT; tick_to(7000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, htr_fsm_set_type(2u, now));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
    TEST_ASSERT_TRUE(view().flags & HTR_F_FAULT_LATCHED);
}
/* Finding 5: a start that falls back to cooldown/standby is a failed start. */
void test_start_falling_to_cooldown_faults_promptly(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_STARTING; tick_to(7000);
    fake_ctx[0].hint=HTR_HINT_COOLDOWN; tick_to(7500);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
}
void test_start_falling_back_to_standby_faults_promptly(void){
    boot_found_3w(); req(1); fake_ctx[0].hint=HTR_HINT_STARTING; tick_to(7000);
    fake_ctx[0].hint=HTR_HINT_OFF; tick_to(7500);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, view().phase);
}
void test_standby_before_heater_acts_on_start_is_not_a_fault(void){
    boot_found_3w(); req(1); tick_to(8000);               /* still reports standby: not yet acted */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, view().phase);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_off_with_heater_reporting_running_sends_stop);
    RUN_TEST(test_stop_requested_needs_a_settled_standby_report);
    RUN_TEST(test_type_change_rejected_in_fault);
    RUN_TEST(test_start_falling_to_cooldown_faults_promptly);
    RUN_TEST(test_start_falling_back_to_standby_faults_promptly);
    RUN_TEST(test_standby_before_heater_acts_on_start_is_not_a_fault);

    RUN_TEST(test_full_cycle_across_tick_wrap);
    RUN_TEST(test_boot_is_tx_quiet_and_iface_off_for_3s);
    RUN_TEST(test_3w_signature_probes_only_3w_drivers_in_order);
    RUN_TEST(test_4w_signature_selects_4w_driver_and_powers_4w_path);
    RUN_TEST(test_push_pull_4w_never_enabled_without_4w_signature);
    RUN_TEST(test_no_heater_retries_every_10s);
    RUN_TEST(test_full_cycle_start_run_stop_cooldown_off);
    RUN_TEST(test_start_timeout_faults_and_stops_without_cutting_power);
    RUN_TEST(test_no_autonomous_restart_after_fault);
    RUN_TEST(test_unexpected_heater_stop_while_running_is_a_fault);
    RUN_TEST(test_comms_loss_is_flag_only);
    RUN_TEST(test_cooldown_timeout_faults_but_keeps_polling);
    RUN_TEST(test_live_level_change_applies_to_driver);
    RUN_TEST(test_hotswap_redetects_only_while_off);
    RUN_TEST(test_forced_type_skips_sense_and_probe);
    RUN_TEST(test_type_change_rejected_while_running);
    return UNITY_END(); }
