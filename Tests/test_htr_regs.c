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

static void boot(void){
    htr_regs_cfg_t rc = { &nv, HTR_HW_ID_EF_L011, (1u<<1)|(1u<<2), 0, 0 };
    uint8_t type = htr_regs_init(&rc, now);
    htr_fsm_cfg_t fc = { drvs, 2, &g.api, false, &u3.api, &u4.api };
    htr_fsm_init(&fc, type, now);
}
void setUp(void){
    fake_drivers_reset(); fake_gpio_init(&g); fake_uart_init(&u3); fake_uart_init(&u4);
    now=0; nv_valid=false; nv_saves=0;
    g.high_pct[HTR_WIRING_3W]=100; fake_ctx[0].probe_answer=HTR_PROBE_FOUND;
}
void tearDown(void){}

void test_identity_block_and_defaults(void){
    boot(); loop_to(100);
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
    TEST_ASSERT_EQUAL_UINT8(HTR_MAP_VERSION, rd8(HTR_R_MAP_VERSION));
    TEST_ASSERT_EQUAL_UINT8(HTR_FW_MINOR, rd8(HTR_R_FW_MINOR));
    TEST_ASSERT_EQUAL_HEX16(0x0006, rd16(HTR_R_SUPPORTED_TYPES));
    TEST_ASSERT_EQUAL_UINT8(HTR_HW_ID_EF_L011, rd8(HTR_R_HW_ID));
    TEST_ASSERT_EQUAL_HEX8(HTR_TYPE_AUTO, rd8(HTR_R_HEATER_TYPE));      /* blank nv -> AUTO */
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT));
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
void test_control_block_write_reaches_driver(void){
    boot(); loop_to(7000);
    const uint8_t ctl[] = { 1, 7, 21, 0xE6, 0x00 };   /* run, level 7, 21 C, ambient 23.0 C */
    mwrite(HTR_R_RUN_REQUEST, ctl, sizeof ctl); loop_to(7100);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    TEST_ASSERT_EQUAL_UINT8(7, fake_ctx[0].last_rq.level);
    TEST_ASSERT_EQUAL_INT8(21, fake_ctx[0].last_rq.setpoint_c);
    TEST_ASSERT_EQUAL_INT16(230, fake_ctx[0].last_rq.ambient_c_x10);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_RUN_REQUEST));
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STARTING, rd8(HTR_R_PHASE));
}
void test_ro_writes_ignored_and_bad_values_rejected(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_PHASE, 3); mwrite1(HTR_R_DEVICE_ID, 0); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_OFF, rd8(HTR_R_PHASE));
    TEST_ASSERT_EQUAL_HEX8(HTR_DEVICE_ID, rd8(HTR_R_DEVICE_ID));
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
    mwrite1(HTR_R_HOST_TIMEOUT, 61); loop_to(7300);
    TEST_ASSERT_EQUAL_UINT8(10, rd8(HTR_R_HOST_TIMEOUT));
    mwrite1(HTR_R_HOST_TIMEOUT, 5); loop_to(7400);
    TEST_ASSERT_EQUAL_UINT8(5, nv_to);
}
void test_persisted_settings_load_at_boot(void){
    nv_valid=true; nv_type=1; nv_to=3; boot(); loop_to(100);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_HEATER_TYPE));
    TEST_ASSERT_EQUAL_UINT8(3, rd8(HTR_R_HOST_TIMEOUT));
}
void test_host_timeout_stops_via_cooldown_path_and_flags(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HOST_TIMEOUT, 1); mwrite1(HTR_R_RUN_REQUEST, 1); loop_to(7100);
    fake_ctx[0].hint = HTR_HINT_RUNNING; loop_to(8000);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));   /* reads count as contact */
    loop_to(8000 + 59000);
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);                        /* not yet: 59 s idle */
    loop_to(8000 + 61000);
    TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    TEST_ASSERT_TRUE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_STOP_REQUESTED, rd8(HTR_R_PHASE));
    TEST_ASSERT_TRUE(g.line[HTR_LINE_TX_EN]);                        /* never a power cut */
    mwrite1(HTR_R_RUN_REQUEST, 0); loop_to(now + 20);
    TEST_ASSERT_FALSE(rd16(HTR_R_FLAGS) & HTR_F_HOST_TIMEOUT_STOP);
}
void test_host_timeout_zero_disables(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_HOST_TIMEOUT, 0); mwrite1(HTR_R_RUN_REQUEST, 1);
    fake_ctx[0].hint = HTR_HINT_RUNNING;          /* healthy run, no G0B1 traffic */
    loop_to(7000 + 30u*60000u);
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_RUNNING, rd8(HTR_R_PHASE));
    TEST_ASSERT_EQUAL(0u, fake_ctx[0].stops);
}
void test_commands(void){
    boot(); loop_to(7000);
    mwrite1(HTR_R_COMMAND, HTR_CMD_START); loop_to(7100);
    TEST_ASSERT_EQUAL_UINT8(1, rd8(HTR_R_RUN_REQUEST)); TEST_ASSERT_EQUAL(1u, fake_ctx[0].starts);
    mwrite1(HTR_R_COMMAND, HTR_CMD_AUTO_DETECT); loop_to(7200);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, rd8(HTR_R_CMD_RESULT));
    mwrite1(HTR_R_COMMAND, HTR_CMD_STOP); loop_to(7300);
    TEST_ASSERT_EQUAL_UINT8(0, rd8(HTR_R_RUN_REQUEST)); TEST_ASSERT_EQUAL(1u, fake_ctx[0].stops);
    mwrite1(HTR_R_COMMAND, 99); loop_to(7400);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BAD_ARG, rd8(HTR_R_CMD_RESULT));
    mwrite1(HTR_R_HEATER_TYPE, HTR_TYPE_NONE); loop_to(30000);
    mwrite1(HTR_R_COMMAND, HTR_CMD_START); loop_to(30100);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_NO_TYPE, rd8(HTR_R_CMD_RESULT));
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
    RUN_TEST(test_control_block_write_reaches_driver);
    RUN_TEST(test_ro_writes_ignored_and_bad_values_rejected);
    RUN_TEST(test_half_written_ambient_is_rejected);
    RUN_TEST(test_heater_type_write_persists_and_validates);
    RUN_TEST(test_persisted_settings_load_at_boot);
    RUN_TEST(test_host_timeout_stops_via_cooldown_path_and_flags);
    RUN_TEST(test_host_timeout_zero_disables);
    RUN_TEST(test_commands);
    RUN_TEST(test_status_block_reflects_driver_and_echo_fault_is_sticky);
    return UNITY_END(); }
