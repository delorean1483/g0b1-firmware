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
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_start_emits_four_start_then_run);
    RUN_TEST(test_running_response_decodes_and_is_not_safe_off);
    RUN_TEST(test_stop_then_state00_sets_safe_off);
    RUN_TEST(test_comms_loss_while_on_is_fault_never_safe); return UNITY_END(); }
