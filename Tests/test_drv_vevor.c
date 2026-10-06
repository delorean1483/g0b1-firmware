#include "drv_vevor.h"
#include "vevor_proto.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_uart_t u;
void setUp(void){ fake_uart_init(&u); drv_vevor.init(drv_vevor.ctx,&u.api,0U); }
void tearDown(void){}

static void reply(uint8_t state, uint8_t level, uint8_t err){
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[5]=state; r[6]=level; r[7]=err; r[11]=125;
    r[55]=VEVOR_Checksum(r,55U); fake_uart_inject(&u,r,56);
}

void test_identity(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_VEVOR, drv_vevor.type_id);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_3W, drv_vevor.wiring);
    TEST_ASSERT_TRUE(drv_vevor.caps & HTR_CAP_LEVEL);
}
void test_probe_found_on_valid_reply_and_sends_only_stop_polls(void){
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_BUSY, drv_vevor.probe(drv_vevor.ctx,0U));
    reply(0,1,0);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, drv_vevor.probe(drv_vevor.ctx,20U));
    for(size_t i=0;i<u.writes;i++){
        TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[u.write_off[i]+2]);    /* control cmd */
        TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[u.write_off[i]+9]);    /* STOP state */
    }
}
void test_probe_absent_after_timeout_with_only_stop_polls(void){
    htr_probe_t r=HTR_PROBE_BUSY; uint32_t t=0;
    while(r==HTR_PROBE_BUSY && t<10000U){ r=drv_vevor.probe(drv_vevor.ctx,t); t+=10U; }
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_ABSENT,r);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3500U,t-10U);
    TEST_ASSERT_GREATER_THAN(0,(int)u.writes);
    for(size_t i=0;i<u.writes;i++) TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[u.write_off[i]+9]);
}
void test_start_uses_request_level(void){
    htr_request_t rq={1,7,20,HTR_AMBIENT_UNKNOWN};
    drv_vevor.start(drv_vevor.ctx,&rq); fake_uart_clear_tx(&u);
    drv_vevor.process(drv_vevor.ctx,1000U);
    TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[2]); TEST_ASSERT_EQUAL_HEX8(0x06,u.tx[9]);   /* START */
    TEST_ASSERT_EQUAL_UINT8(7,u.tx[8]);
}
void test_status_maps_states_and_valid_bits(void){
    htr_status_t s;
    drv_vevor.status(drv_vevor.ctx,0U,&s);
    TEST_ASSERT_FALSE(s.any_rx); TEST_ASSERT_EQUAL_HEX16(0,s.valid);
    const uint8_t st[5]={0,1,2,3,4};
    const uint8_t hint[5]={HTR_HINT_OFF,HTR_HINT_STARTING,HTR_HINT_STARTING,HTR_HINT_RUNNING,HTR_HINT_COOLDOWN};
    for(int i=0;i<5;i++){
        reply(st[i],5,0); drv_vevor.process(drv_vevor.ctx,(uint32_t)(100+i*20));
        drv_vevor.status(drv_vevor.ctx,0U,&s);
        TEST_ASSERT_EQUAL_UINT8(hint[i],s.hint);
    }
    TEST_ASSERT_TRUE(s.any_rx);
    TEST_ASSERT_EQUAL_UINT16(12500,s.supply_mv);
    TEST_ASSERT_TRUE(s.valid & HTR_V_SUPPLY_MV);
    TEST_ASSERT_FALSE(s.valid & HTR_V_EXCH_C);              /* never characterised */
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_probe_found_on_valid_reply_and_sends_only_stop_polls);
    RUN_TEST(test_probe_absent_after_timeout_with_only_stop_polls);
    RUN_TEST(test_start_uses_request_level);
    RUN_TEST(test_status_maps_states_and_valid_bits);
    return UNITY_END(); }
