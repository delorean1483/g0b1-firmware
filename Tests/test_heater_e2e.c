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
static bool cortex_live, reject_stop;
static unsigned stop_writes, start_writes;
static bool fail_start, deliver_start, fail_status;

/* Mem_Write = START addr+W, ptr, data..., STOP. Mem_Read = START addr+W, ptr,
   RESTART addr+R, data..., STOP (no STOP between the two phases). */
static int br_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len){
    (void)ctx;
    if (fail_status && reg == HTR_R_HEATER_TYPE) return -1;
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    htr_regs_i2c_begin(true); for(uint16_t i=0;i<len;i++) buf[i]=htr_regs_i2c_tx(); htr_regs_i2c_end(); return 0;
}
static int br_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len){
    (void)ctx;
    if (reg == HTR_R_RUN_REQUEST && len == 1U && buf[0] == 0U) {
        ++stop_writes;
        if (reject_stop) return -1;
    }
    bool report_failure = false;
    if (reg == HTR_R_RUN_REQUEST && len == 1U && buf[0] == HTR_RUN_KEY) {
        ++start_writes;
        report_failure = fail_start;
        if (fail_start && !deliver_start) return -1;
    }
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg);
    for(uint16_t i=0;i<len;i++) htr_regs_i2c_rx(buf[i]); htr_regs_i2c_end(); return report_failure ? -1 : 0;
}
static const i2c_backend_t bridge = { br_read, br_write, 0 };

static void both_to(uint32_t t){
    for(; now < t; now += 10U){
        htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now, 0);
        if (cortex_live) heater_note_cortex_traffic(now);
        heater_process(now);
    }
}
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }

static void coproc_boot(void){
    htr_regs_cfg_t rc = { 0, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
}
void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4); now = 0; cortex_live = true; reject_stop = false; stop_writes = start_writes = 0; fail_start = deliver_start = fail_status = false;
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
void test_fresh_cortex_traffic_keeps_v2_watchdog_alive(void){
    both_to(7000); heater_set_request(1); both_to(7600);   /* request reaches the coprocessor first */
    fake_ctx[0].hint = HTR_HINT_RUNNING;
    both_to(7000 + 15u * 60000u);                             /* 15 min > default 10 min timeout */
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, tel().phase);
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
void test_cortex_silence_sends_verified_stop(void)
{
    both_to(7000); heater_set_request(1); both_to(7600);
    fake_ctx[0].hint = HTR_HINT_RUNNING;
    cortex_live = false;
    both_to(14000);
    TEST_ASSERT_TRUE(stop_writes >= 1);
    TEST_ASSERT_EQUAL_UINT(1, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
}

void test_failed_silence_stop_falls_back_to_v2_watchdog(void)
{
    both_to(7000); heater_set_request(1); both_to(7600);
    TEST_ASSERT_EQUAL_UINT8(10, htr_regs_host_timeout_s());
    fake_ctx[0].hint = HTR_HINT_RUNNING;
    reject_stop = true;
    cortex_live = false;
    both_to(15000);
    TEST_ASSERT_EQUAL_UINT(4, stop_writes);
    TEST_ASSERT_EQUAL_UINT(0, fake_ctx[0].stops);
    both_to(22600);
    uint8_t status[2];
    br_read(NULL, HTR_R_FLAGS, status, 2U);
    TEST_ASSERT_BITS_HIGH(HTR_F_HOST_TIMEOUT_STOP, status[0]);
    TEST_ASSERT_EQUAL_UINT(4, stop_writes);
    TEST_ASSERT_EQUAL_UINT(1, fake_ctx[0].stops);
}

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
static void test_ambiguous_start_delivered(void)
{
    both_to(7000);
    unsigned stops = stop_writes;
    fail_start = deliver_start = true;
    TEST_ASSERT_TRUE(heater_set_request(1));
    both_to(8000);
    TEST_ASSERT_EQUAL_UINT(1, start_writes);
    TEST_ASSERT_EQUAL_UINT(1, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT(stops, stop_writes);
    TEST_ASSERT_EQUAL_UINT8(1, heater_get_request());
}

static void test_ambiguous_start_not_delivered(void)
{
    both_to(7000);
    fail_start = true;
    TEST_ASSERT_TRUE(heater_set_request(1));
    both_to(8000);
    TEST_ASSERT_EQUAL_UINT(1, start_writes);
    TEST_ASSERT_EQUAL_UINT(0, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_REQUEST_AGAIN, tel().flags);
}

static void test_ambiguous_start_expires_without_status(void)
{
    both_to(7000);
    fail_start = deliver_start = true;
    TEST_ASSERT_TRUE(heater_set_request(1));
    while (start_writes == 0) both_to(now + 10U);
    fail_status = true;
    both_to(now + HEATER_LOST_MS + 10U);
    TEST_ASSERT_EQUAL_UINT(1, start_writes);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_REQUEST_AGAIN, tel().flags);
}

int main(void){ UNITY_BEGIN();
    RUN_TEST(test_ambiguous_start_delivered);
    RUN_TEST(test_ambiguous_start_not_delivered);
    RUN_TEST(test_ambiguous_start_expires_without_status);
    RUN_TEST(test_fault_then_clear_from_g0b1_does_not_reignite);

    RUN_TEST(test_coprocessor_reset_does_not_restart_on_a_stale_request);
    RUN_TEST(test_start_command_starts_through_the_request);
    RUN_TEST(test_full_chain_detect_start_run_stop_cooldown);
    RUN_TEST(test_failed_silence_stop_falls_back_to_v2_watchdog);
    RUN_TEST(test_cortex_silence_sends_verified_stop);
    RUN_TEST(test_fresh_cortex_traffic_keeps_v2_watchdog_alive);
    return UNITY_END(); }
