#include "htr_regs.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_version.h"
#include "fake_htr_driver.h"
#include "fake_htr_gpio.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_gpio_t g; static fake_uart_t u3, u4;
static const htr_driver_t *const drvs[] = { &fake_drv[0], &fake_drv[1] };
static uint32_t now;
/* fake nv */
static bool nv_valid; static uint8_t nv_type, nv_to; static unsigned nv_saves;
static bool nv_load(void *c, uint8_t *t, uint8_t *to){ (void)c; if(!nv_valid) return false; *t=nv_type; *to=nv_to; return true; }
static void nv_save(void *c, uint8_t t, uint8_t to){ (void)c; nv_valid=true; nv_type=t; nv_to=to; nv_saves++; }
static const htr_nv_t nv = { nv_load, nv_save, 0 };

/* I2C master helpers (what the G0B1's Mem_Write / Mem_Read look like). */
static void mwrite(uint8_t reg, const uint8_t *d, size_t n){
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg); for(size_t i=0;i<n;i++) htr_regs_i2c_rx(d[i]); htr_regs_i2c_end();
}
static void mwrite1(uint8_t reg, uint8_t v){ mwrite(reg,&v,1); }
static void mread(uint8_t reg, uint8_t *d, size_t n){
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(reg); htr_regs_i2c_end();     /* set pointer */
    htr_regs_i2c_begin(true); for(size_t i=0;i<n;i++) d[i]=htr_regs_i2c_tx(); htr_regs_i2c_end();
}
static uint8_t rd8(uint8_t reg){ uint8_t v; mread(reg,&v,1); return v; }
static uint16_t rd16(uint8_t reg){ uint8_t v[2]; mread(reg,v,2); return (uint16_t)(v[0]|(v[1]<<8)); }
static void loop_to(uint32_t t){ for(; now<t; now+=10u){ htr_regs_poll(now); htr_fsm_process(now); htr_regs_publish(now,0); } }
/* Map v2 host side: a changing heartbeat keeps the host live; RUN_REQUEST = 0
   is the clean stop that arms the next keyed start. */
static uint8_t hbv;
static void hb(void){ mwrite1(HTR_R_HOST_HEARTBEAT, ++hbv); }
static void arm(void){ mwrite1(HTR_R_RUN_REQUEST, 0); }
static void live_armed(void){ hb(); arm(); loop_to(now + 20u); }
static void start_keyed(void){ hb(); mwrite1(HTR_R_RUN_REQUEST, HTR_RUN_KEY); loop_to(now + 100u); }
static void loop_hb_to(uint32_t t){ while(now < t){ hb(); loop_to(((t - now) > 250u) ? now + 250u : t); } }
static void settle_off(void){ fake_ctx[0].hint = HTR_HINT_OFF; loop_hb_to(now + HTR_STOP_SETTLE_MS + 1000u); }

static void boot(void){
    htr_regs_cfg_t rc = { &nv, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
}
void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4);
    now=0; nv_valid=false; nv_saves=0; hbv=0;
    g.high_pct[HTR_WIRING_3W]=100; fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
}
void tearDown(void){}

void test_identity_block_and_defaults(void){
    boot(); loop_to(100);
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
    TEST_ASSERT_EQUAL_UINT8(2, rd8(HTR_R_MAP_VERSION));               /* keyed start = map v2 */
    TEST_ASSERT_EQUAL_UINT8(HTR_FW_MINOR, rd8(HTR_R_FW_MINOR));
    TEST_ASSERT_EQUAL_HEX16(0x0006, rd16(HTR_R_SUPPORTED_TYPES));
    TEST_ASSERT_EQUAL_UINT8(HTR_HW_ID_EF_L011, rd8(HTR_R_HW_ID));
    TEST_ASSERT_EQUAL_HEX8(HTR_TYPE_AUTO, rd8(HTR_R_HEATER_TYPE));      /* blank nv -> AUTO */
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT_S));
    TEST_ASSERT_EQUAL_UINT16(0, rd16(HTR_R_WRITES_DISCARDED));
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & (HTR_F_HOST_LIVE | HTR_F_START_ARMED));
    TEST_ASSERT_EQUAL_HEX8(0xFF, rd8(0x7F));                            /* beyond the map */
}
void test_read_is_one_consistent_snapshot(void){
    boot(); loop_to(100);
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(HTR_R_PHASE); htr_regs_i2c_end();
    htr_regs_i2c_begin(true);
    uint8_t p0 = htr_regs_i2c_tx();
    loop_to(7000);                    /* detection completes mid-transaction */
    uint8_t t0 = htr_regs_i2c_tx();   /* ACTIVE_TYPE from the same snapshot */
    htr_regs_i2c_end();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_DETECTING, p0);
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_NONE, t0);
    TEST_ASSERT_EQUAL_UINT8(1u, rd8(HTR_R_ACTIVE_TYPE));               /* next read sees it */
}
void test_keyed_control_block_reaches_driver(void){
    boot(); loop_to(7000); live_armed();
    const uint8_t ctl[] = { HTR_RUN_KEY, 7, 21, 0xE6, 0x00 };   /* run, level 7, 21 C, ambient 23.0 C */
    hb(); mwrite(HTR_R_RUN_REQUEST, ctl, sizeof ctl); loop_to(now + 100u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(7, fake_ctx[0].last_rq.level);
    TEST_ASSERT_EQUAL_INT8(21, fake_ctx[0].last_rq.setpoint_c);
    TEST_ASSERT_EQUAL_INT16(230, fake_ctx[0].last_rq.ambient_c_x10);
    TEST_ASSERT_EQUAL_HEX8(HTR_RUN_KEY, rd8(HTR_R_RUN_REQUEST));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, rd8(HTR_R_PHASE));
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_START_ARMED);         /* the start used the arm */
    hb(); mwrite1(HTR_R_RUN_REQUEST, HTR_RUN_KEY); loop_to(now + 100u); /* replay while on: no-op */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_OK, rd8(HTR_R_CMD_RESULT));
}
void test_v1_run_value_never_starts(void){                 /* an old G0B1 writing run = 1 */
    boot(); loop_to(7000); live_armed();
    hb(); mwrite1(HTR_R_RUN_REQUEST, 1); loop_to(now + 100u);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST));
}
void test_ro_writes_ignored_and_bad_values_rejected(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_PHASE, 3); mwrite1(HTR_R_DEVICE_ID, 0); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
    TEST_ASSERT_EQUAL_UINT16(2, rd16(HTR_R_WRITES_DISCARDED));       /* outside the control block */
    mwrite1(HTR_R_LEVEL, 11); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_LEVEL));
    mwrite1(HTR_R_SETPOINT_C, 40); mwrite1(HTR_R_RUN_REQUEST, 2); loop_to(7300);
    TEST_ASSERT_EQUAL_INT8(20, (int8_t)rd8(HTR_R_SETPOINT_C));
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST));
}
void test_half_written_ambient_is_rejected(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_AMBIENT_C_X10, 0x10); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_HEX16(0x7FFF, rd16(HTR_R_AMBIENT_C_X10));
}
void test_heater_type_write_persists_and_validates(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HEATER_TYPE, 9); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_SUPPORTED, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(0u, nv_saves);
    mwrite1(HTR_R_HEATER_TYPE, 2); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_OK, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(1u, nv_saves); TEST_ASSERT_EQUAL_UINT8(2, nv_type);
    TEST_ASSERT_EQUAL_UINT8(2, rd8(HTR_R_HEATER_TYPE));
    /* The old minutes byte stays the v1 default so a rolled-back image keeps a timeout. */
    TEST_ASSERT_EQUAL_UINT8(HTR_NV_LEGACY_TIMEOUT_MIN, nv_to);
    TEST_ASSERT_EQUAL_UINT8(10, HTR_NV_LEGACY_TIMEOUT_MIN);
}
void test_host_timeout_is_seconds_in_range_and_not_persisted(void){
    boot(); loop_to(7000);
    const uint8_t bad[] = { 0, 2, 61 };                       /* 0 no longer disables */
    for (unsigned i = 0; i < sizeof bad; i++) {
        mwrite1(HTR_R_HOST_TIMEOUT_S, bad[i]); loop_to(now + 100u);
        TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
        TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT_S));
    }
    mwrite1(HTR_R_HOST_TIMEOUT_S, 5); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(5, rd8(HTR_R_HOST_TIMEOUT_S));
    TEST_ASSERT_EQUAL(0u, nv_saves);
    boot(); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT_S));
}
void test_persisted_type_loads_and_legacy_minutes_byte_is_ignored(void){
    nv_valid=true; nv_type=1; nv_to=0; boot(); loop_to(100);    /* v1 "disabled" byte */
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_HEATER_TYPE));
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT_S));
}
void test_start_needs_a_live_heartbeat(void){
    boot(); loop_to(7000); arm(); loop_to(7100);              /* armed, no heartbeat yet */
    mwrite1(HTR_R_RUN_REQUEST, HTR_RUN_KEY); loop_to(7200);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NO_HOST, rd8(HTR_R_CMD_RESULT));
    uint16_t f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_START_REFUSED); TEST_ASSERT_FALSE(f & HTR_F_HOST_LIVE);
    start_keyed();                                            /* heartbeat + key in one go */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_HOST_LIVE); TEST_ASSERT_FALSE(f & HTR_F_START_REFUSED);
}
void test_heartbeat_and_key_in_one_block_starts(void){
    boot(); loop_to(7000); arm(); loop_to(7100);
    const uint8_t ctl[] = { HTR_RUN_KEY, 3, 20, 0xFF, 0x7F, 10, 0x41 };   /* 0x11..0x17 */
    mwrite(HTR_R_RUN_REQUEST, ctl, sizeof ctl); loop_to(7200);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
}
void test_start_needs_a_clean_stop_first(void){
    boot(); loop_to(7000); hb(); loop_to(7100);
    hb(); mwrite1(HTR_R_RUN_REQUEST, HTR_RUN_KEY); loop_to(7200);  /* fresh boot: not armed */
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_ARMED, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_START_REFUSED);
    arm(); loop_to(7300);
    uint16_t f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_START_ARMED); TEST_ASSERT_FALSE(f & HTR_F_START_REFUSED);
    start_keyed();
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
}
void test_reads_and_a_stuck_heartbeat_do_not_keep_the_host_live(void){
    boot(); loop_to(7000); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_hb_to(now + 1000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));
    uint8_t stuck = hbv; uint32_t t0 = now;
    while (now < t0 + 9500u) { mwrite1(HTR_R_HOST_HEARTBEAT, stuck); (void)rd8(HTR_R_PHASE); loop_to(now + 250u); }
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);                 /* not yet: under 10 s */
    while (now < t0 + 10500u) { mwrite1(HTR_R_HOST_HEARTBEAT, stuck); (void)rd8(HTR_R_PHASE); loop_to(now + 250u); }
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    uint16_t f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_HOST_TIMEOUT_STOP); TEST_ASSERT_FALSE(f & HTR_F_HOST_LIVE);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, rd8(HTR_R_PHASE));
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);                 /* controlled stop, never a power cut */
}
void test_changing_heartbeat_keeps_the_host_live(void){
    boot(); loop_to(7000); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_hb_to(now + 15u * 60000u);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_HOST_LIVE);
}
void test_host_timeout_stop_needs_a_fresh_request(void){
    boot(); loop_to(7000); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_hb_to(now + 1000u);
    loop_to(now + 11000u);                                    /* host silent past 10 s */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    settle_off();
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    start_keyed();                                            /* host back, replays its old block */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_ARMED, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
    arm(); loop_to(now + 20u);                                /* host acknowledges with a stop */
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
    start_keyed();
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
void test_host_timeout_setting_applies(void){
    boot(); loop_to(7000); mwrite1(HTR_R_HOST_TIMEOUT_S, 5); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_to(now + 4500u);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
    loop_to(now + 1000u);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
}
void test_host_idle_counts_from_the_last_valid_heartbeat(void){
    boot(); loop_to(7000); hb(); loop_to(now + 3500u);
    TEST_ASSERT_EQUAL_UINT16(3, rd16(HTR_R_HOST_IDLE_S));
    hb(); loop_to(now + 20u);
    TEST_ASSERT_EQUAL_UINT16(0, rd16(HTR_R_HOST_IDLE_S));
}
void test_commands(void){
    boot(); loop_to(7000); live_armed();
    hb(); mwrite1(HTR_R_COMMAND, HTR_CMD_START); loop_to(now + 100u); /* only RUN_REQUEST starts */
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_SUPPORTED, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    start_keyed(); TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    mwrite1(HTR_R_COMMAND, HTR_CMD_AUTO_DETECT); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, rd8(HTR_R_CMD_RESULT));
    mwrite1(HTR_R_COMMAND, HTR_CMD_STOP); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST)); TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_START_ARMED);         /* a STOP command arms too */
    mwrite1(HTR_R_COMMAND, 99); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    settle_off();                                             /* stop settles to OFF before a type change */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    mwrite1(HTR_R_HEATER_TYPE, HTR_TYPE_NONE); loop_hb_to(now + 23000u);
    start_keyed();
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NO_TYPE, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST));
}
void test_aborted_write_is_discarded(void){
    boot(); loop_to(7000); live_armed();
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(HTR_R_RUN_REQUEST); htr_regs_i2c_rx(HTR_RUN_KEY);
    htr_regs_i2c_error();                                     /* BERR mid-write, no STOP */
    hb(); mwrite1(HTR_R_LEVEL, 4); loop_to(now + 100u);       /* a later clean write commits only itself */
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(4, rd8(HTR_R_LEVEL));
    TEST_ASSERT_EQUAL_UINT16(1, rd16(HTR_R_WRITES_DISCARDED));
}
void test_write_cut_by_a_repeated_start_is_discarded(void){
    boot(); loop_to(7000); live_armed();
    htr_regs_i2c_begin(false); htr_regs_i2c_rx(HTR_R_RUN_REQUEST); htr_regs_i2c_rx(HTR_RUN_KEY);
    htr_regs_i2c_begin(true); (void)htr_regs_i2c_tx(); htr_regs_i2c_end();
    hb(); loop_to(now + 100u);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT16(1, rd16(HTR_R_WRITES_DISCARDED));
}
void test_write_reaching_outside_the_control_block_is_discarded_whole(void){
    boot(); loop_to(7000);
    const uint8_t d[] = { 0, 0, 0, 4 };                       /* 0x0F (RO) .. 0x12 LEVEL */
    mwrite(0x0F, d, sizeof d); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_LEVEL));
    TEST_ASSERT_EQUAL_UINT16(1, rd16(HTR_R_WRITES_DISCARDED));
}
void test_fault_drop_disarms_until_a_clean_stop(void){
    boot(); loop_to(7000); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_FAULT; loop_hb_to(now + 500u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_FAULT, rd8(HTR_R_PHASE));
    fake_ctx[0].hint = HTR_HINT_OFF; loop_hb_to(now + 500u);
    mwrite1(HTR_R_COMMAND, HTR_CMD_CLEAR_FAULT); loop_hb_to(now + 500u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    start_keyed();                                            /* replayed key after the fault */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_ARMED, rd8(HTR_R_CMD_RESULT));
    arm(); start_keyed();
    TEST_ASSERT_EQUAL(2u, fake_ctx[0].starts);
}
void test_stop_is_kept_when_a_later_write_lands_before_the_poll(void){
    boot(); loop_to(7000); hb(); loop_to(7100);
    arm(); hb(); mwrite1(HTR_R_RUN_REQUEST, HTR_RUN_KEY); loop_to(7200);  /* no poll between */
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_hb_to(8000);
    mwrite1(HTR_R_COMMAND, HTR_CMD_STOP); mwrite1(HTR_R_COMMAND, HTR_CMD_CLEAR_FAULT); loop_to(8100);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);                 /* the STOP wasn't overwritten away */
}
void test_garbage_run_value_stops_without_arming(void){
    boot(); loop_to(7000); live_armed(); start_keyed();
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_hb_to(now + 1000u);
    hb(); mwrite1(HTR_R_RUN_REQUEST, 0x37); loop_to(now + 100u);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    settle_off(); start_keyed();
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NOT_ARMED, rd8(HTR_R_CMD_RESULT));
}
void test_status_block_reflects_driver_and_echo_fault_is_sticky(void){
    boot(); loop_to(7000);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_ACTIVE_TYPE));
    TEST_ASSERT_EQUAL_HEX16(1u << 0, rd16(HTR_R_CAPS));
    uint16_t f = rd16(HTR_R_FLAGS);
    TEST_ASSERT_TRUE(f & HTR_F_PRESENT); TEST_ASSERT_TRUE(f & HTR_F_FRESH);
    TEST_ASSERT_TRUE(f & HTR_F_SAFE_OFF); TEST_ASSERT_TRUE(f & HTR_F_IFACE_POWERED);
    htr_regs_publish(now, 5);
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_ECHO_FAULT);
    TEST_ASSERT_EQUAL_UINT16(5, rd16(HTR_R_ECHO_ERRS));
    now += 3100; htr_regs_publish(now, 5);
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_ECHO_FAULT);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity_block_and_defaults);
    RUN_TEST(test_read_is_one_consistent_snapshot);
    RUN_TEST(test_keyed_control_block_reaches_driver);
    RUN_TEST(test_v1_run_value_never_starts);
    RUN_TEST(test_ro_writes_ignored_and_bad_values_rejected);
    RUN_TEST(test_half_written_ambient_is_rejected);
    RUN_TEST(test_heater_type_write_persists_and_validates);
    RUN_TEST(test_host_timeout_is_seconds_in_range_and_not_persisted);
    RUN_TEST(test_persisted_type_loads_and_legacy_minutes_byte_is_ignored);
    RUN_TEST(test_start_needs_a_live_heartbeat);
    RUN_TEST(test_heartbeat_and_key_in_one_block_starts);
    RUN_TEST(test_start_needs_a_clean_stop_first);
    RUN_TEST(test_reads_and_a_stuck_heartbeat_do_not_keep_the_host_live);
    RUN_TEST(test_changing_heartbeat_keeps_the_host_live);
    RUN_TEST(test_host_timeout_stop_needs_a_fresh_request);
    RUN_TEST(test_host_timeout_setting_applies);
    RUN_TEST(test_host_idle_counts_from_the_last_valid_heartbeat);
    RUN_TEST(test_commands);
    RUN_TEST(test_aborted_write_is_discarded);
    RUN_TEST(test_write_cut_by_a_repeated_start_is_discarded);
    RUN_TEST(test_write_reaching_outside_the_control_block_is_discarded_whole);
    RUN_TEST(test_fault_drop_disarms_until_a_clean_stop);
    RUN_TEST(test_stop_is_kept_when_a_later_write_lands_before_the_poll);
    RUN_TEST(test_garbage_run_value_stops_without_arming);
    RUN_TEST(test_status_block_reflects_driver_and_echo_fault_is_sticky);
    return UNITY_END(); }
