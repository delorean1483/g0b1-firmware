/* End-to-end on the host: G0B1 heater client -> I2C bridge -> REAL coprocessor
   register file + FSM (fake heater driver). Proves the two sides of the
   frozen htr_i2c_map.h contract agree. */
#include "heater.h"
#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const drvs[] = { &fake_drv[0], &fake_drv[1] };
static uint32_t now;

/* Mem_Write = START addr+W, ptr, data..., STOP. Mem_Read = START addr+W, ptr,
   RESTART addr+R, data..., STOP (no STOP between the two phases). */
static int br_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len){
    (void)ctx; htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    htr_regs_i2c_begin(true); for(uint16_t i=0;i<len;i++) buf[i]=htr_regs_i2c_tx(); htr_regs_i2c_end(); return 0;
}
static int br_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len){
    (void)ctx; htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    for(uint16_t i=0;i<len;i++) htr_regs_i2c_rx(buf[i]); htr_regs_i2c_end(); return 0;
}
static const i2c_backend_t bridge = { br_read, br_write, 0 };

static void both_to(uint32_t t){
    for(; now < t; now += 10U){
        htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now, 0);
        heater_process(now);
    }
}
/* The G0B1 hung or busy: only the coprocessor runs. */
static void coproc_only_to(uint32_t t){
    for(; now < t; now += 10U){ htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now, 0); }
}
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }
static uint8_t coproc_phase(void){ htr_fsm_view_t v; htr_fsm_view(now, &v); return v.phase; }

static void coproc_boot(void){
    htr_regs_cfg_t rc = { 0, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
}
void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4); now = 0;
    g.high_pct[HTR_WIRING_3W] = 100; fake_ctx[0].probe_answer = HTR_PROBE_FOUND;
    coproc_boot();
    heater_init(&bridge, 0, now);
}
void tearDown(void){}

void test_full_chain_detect_start_run_stop_cooldown(void){
    both_to(7000);
    heater_telemetry_t t = tel();
    TEST_ASSERT_TRUE(t.present);
    TEST_ASSERT_EQUAL_UINT8(1, t.type);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, t.phase);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_SAFE_OFF);
    heater_set_level(8); heater_set_request(1); both_to(7600);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(8, fake_ctx[0].last_rq.level);
    TEST_ASSERT_EQUAL_UINT8(1, tel().state);                  /* legacy starting */
    fake_ctx[0].hint = HTR_HINT_RUNNING; both_to(8500);
    TEST_ASSERT_EQUAL_UINT8(3, tel().state);
    heater_set_request(0); both_to(9000);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    fake_ctx[0].hint = HTR_HINT_COOLDOWN; both_to(10000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, tel().phase);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_COOLDOWN);
    fake_ctx[0].hint = HTR_HINT_OFF; both_to(11000);
    TEST_ASSERT_EQUAL_UINT8(0, tel().state);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_SAFE_OFF);
}
void test_g0b1_heartbeat_keeps_the_host_live(void){
    both_to(7000); heater_set_request(1); both_to(7600);   /* request reaches the coprocessor first */
    fake_ctx[0].hint = HTR_HINT_RUNNING;
    both_to(7000 + 15u * 60000u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, tel().phase);
}
/* Robb's validation list: loss of valid host traffic beyond the timeout ->
   controlled stop; recovery -> no restart without a fresh request. */
void test_g0b1_silence_stops_and_recovery_needs_a_fresh_request(void){
    both_to(7000); heater_set_request(1); both_to(7600);
    fake_ctx[0].hint = HTR_HINT_RUNNING; both_to(9000);
    coproc_only_to(9000 + 9000u);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);                 /* under 10 s: still running */
    coproc_only_to(9000 + 11000u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    fake_ctx[0].hint = HTR_HINT_COOLDOWN; coproc_only_to(now + 1000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, coproc_phase());
    both_to(now + 2000u);                                     /* G0B1 back, still asking for heat */
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());          /* latched off, not replayed */
    fake_ctx[0].hint = HTR_HINT_OFF; both_to(now + 5000u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, tel().phase);
    heater_set_request(1); both_to(now + 600u);              /* fresh request */
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
/* Coprocessor reset mid-run while the G0B1 still asks for heat: no restart. */
void test_coprocessor_reset_does_not_restart_on_a_stale_request(void){
    both_to(7000); heater_set_request(1); both_to(7600);
    fake_ctx[0].hint = HTR_HINT_RUNNING; both_to(9000);
    coproc_boot(); fake_ctx[0].hint = HTR_HINT_OFF;           /* reboot; heater stopped with it */
    both_to(9000 + 15000u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
}
/* Modbus reg 75 START reaches the heater through the keyed request. */
void test_start_command_starts_through_the_request(void){
    both_to(7000);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_START)); both_to(7600);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_STOP)); both_to(8200);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
}
/* Finding 3, end to end: fault -> request latched off -> clear-fault from
   the G0B1 -> OFF, and the heater is NOT restarted until a new ON. */
void test_fault_then_clear_from_g0b1_does_not_reignite(void){
    both_to(7000); heater_set_request(1); fake_ctx[0].hint = HTR_HINT_FAULT; both_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, tel().phase);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    fake_ctx[0].hint = HTR_HINT_OFF; both_to(9000);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT)); both_to(10000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, tel().phase);
    both_to(15000);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    heater_set_request(1); both_to(15600);
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_fault_then_clear_from_g0b1_does_not_reignite);

    RUN_TEST(test_full_chain_detect_start_run_stop_cooldown);
    RUN_TEST(test_g0b1_heartbeat_keeps_the_host_live);
    RUN_TEST(test_g0b1_silence_stops_and_recovery_needs_a_fresh_request);
    RUN_TEST(test_coprocessor_reset_does_not_restart_on_a_stale_request);
    RUN_TEST(test_start_command_starts_through_the_request);
    return UNITY_END(); }
