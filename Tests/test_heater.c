#include "heater.h"
#include "fake_wk2132_i2c.h"
#include "unity.h"
#include <string.h>

/* Build a 56-byte heater frame with a given state/level and valid checksum. */
static void make_resp(uint8_t st, uint8_t lvl, uint8_t err, uint8_t out[56])
{
    memset(out, 0, 56);
    out[0] = 0xAA;
    out[1] = 0x77;
    out[5] = st;
    out[6] = lvl;
    out[7] = err;
    out[55] = VEVOR_Checksum(out, 55);
}

void setUp(void){ fake_wk2132_reset(); heater_init(fake_wk2132_i2c(), 0U); }
void tearDown(void){}

void test_start_emits_four_start_then_run(void){
    heater_set_level(3U); heater_set_request(1U);
    /* 1 s TX cadence: advance time and process 5 times, capturing each TX. */
    uint8_t seen_cmd[5]; uint32_t t=0;
    for(int i=0;i<5;i++){ fake_wk2132_reset_tx(); heater_process(t);
        seen_cmd[i]= fake_wk2132_tx_len()>=3 ? fake_wk2132_tx_data()[2] : 0xFF; t+=1000; }
    /* first four are START (cmd 0x06), then RUN (cmd 0x02) */
    TEST_ASSERT_EQUAL_HEX8(0x06,seen_cmd[0]); TEST_ASSERT_EQUAL_HEX8(0x06,seen_cmd[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02,seen_cmd[4]);
    /* level byte is 3 */
    TEST_ASSERT_EQUAL_HEX8(3U, fake_wk2132_tx_data()[8]);
}
void test_running_response_decodes_and_is_not_safe_off(void){
    heater_set_request(1U); uint8_t r[56]; make_resp(3U,3U,0U,r);
    fake_wk2132_inject_rx(r,56); heater_process(0U); heater_process(20U);
    heater_telemetry_t t; heater_get_telemetry(&t);
    TEST_ASSERT_EQUAL_UINT8(3U,t.state);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_FRESH);
    TEST_ASSERT_FALSE(t.flags & HEATER_FLAG_SAFE_OFF); /* running != safe */
}
void test_stop_then_state00_sets_safe_off(void){
    heater_set_request(0U); uint8_t r[56]; make_resp(0U,0U,0U,r);
    fake_wk2132_inject_rx(r,56); heater_process(0U); heater_process(20U);
    heater_telemetry_t t; heater_get_telemetry(&t);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_SAFE_OFF);
    /* TX should be STOP frames (cmd 0x06, state byte 0x05) */
    TEST_ASSERT_EQUAL_HEX8(0x06, fake_wk2132_tx_data()[2]);
    TEST_ASSERT_EQUAL_HEX8(0x05, fake_wk2132_tx_data()[9]);
}
void test_idle_heater_that_answers_stays_fresh(void){
    /* Heater off (never requested). The controller must keep polling with a
       STOP frame every second so an idle heater that answers each poll stays
       FRESH — the agent derives heater_comms_ok = FRESH && !COMMS_FAULT, so a
       stale idle link would show a false NO COMMS badge on the panel. */
    heater_set_request(0U);
    uint8_t r[56]; make_resp(0U,0U,0U,r);
    uint32_t t=0;
    for(int i=0;i<30;i++){               /* 30 s idle, well past the 3 s window */
        fake_wk2132_reset_tx();
        heater_process(t);               /* TX slot: a STOP poll goes out */
        TEST_ASSERT_TRUE(fake_wk2132_tx_len() >= 10);
        TEST_ASSERT_EQUAL_HEX8(0x06, fake_wk2132_tx_data()[2]);
        TEST_ASSERT_EQUAL_HEX8(0x05, fake_wk2132_tx_data()[9]);
        fake_wk2132_inject_rx(r,56);     /* heater answers "off" */
        heater_process(t+20U);           /* RX poll picks the reply up */
        t+=1000;
    }
    heater_telemetry_t tel; heater_get_telemetry(&tel);
    TEST_ASSERT_TRUE (tel.flags & HEATER_FLAG_FRESH);
    TEST_ASSERT_FALSE(tel.flags & HEATER_FLAG_COMMS_FAULT);
    TEST_ASSERT_TRUE (tel.flags & HEATER_FLAG_SAFE_OFF);
}
void test_comms_loss_while_on_is_fault_never_safe(void){
    heater_set_request(1U); uint8_t r[56]; make_resp(3U,3U,0U,r);
    fake_wk2132_inject_rx(r,56); heater_process(0U); heater_process(20U);
    /* no more responses; advance past freshness (3 s) */
    for(uint32_t t=1000;t<=6000;t+=1000) heater_process(t);
    heater_telemetry_t tel; heater_get_telemetry(&tel);
    TEST_ASSERT_FALSE(tel.flags & HEATER_FLAG_FRESH);
    TEST_ASSERT_TRUE (tel.flags & HEATER_FLAG_COMMS_FAULT);
    TEST_ASSERT_FALSE(tel.flags & HEATER_FLAG_SAFE_OFF); /* loss is never safe-off */
}
void test_reassert_request_while_running_does_not_rearm_start(void){
    heater_set_level(3U); heater_set_request(1U);
    /* Drive through the 4-frame START burst into RUN (mirrors
       test_start_emits_four_start_then_run). */
    uint8_t seen_cmd[5]; uint32_t t=0;
    for(int i=0;i<5;i++){ fake_wk2132_reset_tx(); heater_process(t);
        seen_cmd[i]= fake_wk2132_tx_len()>=3 ? fake_wk2132_tx_data()[2] : 0xFF; t+=1000; }
    TEST_ASSERT_EQUAL_HEX8(0x02,seen_cmd[4]); /* now steady-state RUN */

    /* Re-write request=1 while already on: must be a no-op -- it must NOT
       re-arm a fresh 4-frame START burst. */
    heater_set_request(1U);
    for(int i=0;i<4;i++){
        fake_wk2132_reset_tx();
        heater_process(t);
        TEST_ASSERT_EQUAL_HEX8(0x02, fake_wk2132_tx_data()[2]); /* still RUN, never 0x06 START again */
        t+=1000;
    }
}
void test_xport_fault_is_sticky_then_clears(void){
    /* Force the I2C backend to fail so VEVOR_Process records a transport
       error on its next RX-poll/TX-cadence op. */
    fake_wk2132_force_error(1);
    heater_process(10U); /* >= VEVOR_RX_POLL_INTERVAL_MS(10) since init at t=0 -> triggers an op */
    heater_telemetry_t t;
    heater_get_telemetry(&t);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_XPORT_FAULT);

    fake_wk2132_force_error(0);
    heater_process(500U); /* no new error, but still within the sticky window */
    heater_get_telemetry(&t);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_XPORT_FAULT);

    heater_process(5000U); /* past the sticky window */
    heater_get_telemetry(&t);
    TEST_ASSERT_FALSE(t.flags & HEATER_FLAG_XPORT_FAULT);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_start_emits_four_start_then_run);
    RUN_TEST(test_running_response_decodes_and_is_not_safe_off);
    RUN_TEST(test_stop_then_state00_sets_safe_off);
    RUN_TEST(test_idle_heater_that_answers_stays_fresh);
    RUN_TEST(test_comms_loss_while_on_is_fault_never_safe);
    RUN_TEST(test_reassert_request_while_running_does_not_rearm_start);
    RUN_TEST(test_xport_fault_is_sticky_then_clears); return UNITY_END(); }
