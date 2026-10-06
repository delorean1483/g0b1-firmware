#include "autoterm_proto.h"
#include "htr_types.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>
void setUp(void){} void tearDown(void){}

void test_builders_byte_exact_vs_captures(void){
    uint8_t f[AT_MAX_FRAME];
    const uint8_t wake[]={0xAA,0x03,0x00,0x00,0x1C,0x95,0x3D};
    const uint8_t ver[] ={0xAA,0x03,0x00,0x00,0x06,0x5E,0xBC};
    const uint8_t stat[]={0xAA,0x03,0x00,0x00,0x0F,0x58,0x7C};
    const uint8_t sets[]={0xAA,0x03,0x00,0x00,0x02,0x9D,0xBD};
    const uint8_t stop[]={0xAA,0x03,0x00,0x00,0x03,0x5D,0x7C};
    const uint8_t t23[] ={0xAA,0x03,0x01,0x00,0x11,0x17,0xB3,0x11};
    const uint8_t st29[]={0xAA,0x03,0x07,0x00,0x01,0xFF,0xFF,0x02,0x1D,0x01,0xFF,0x11,0x19,0x34};
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_WAKE,f));     TEST_ASSERT_EQUAL_HEX8_ARRAY(wake,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_VERSION,f));  TEST_ASSERT_EQUAL_HEX8_ARRAY(ver,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_STATUS,f));   TEST_ASSERT_EQUAL_HEX8_ARRAY(stat,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_SETTINGS,f)); TEST_ASSERT_EQUAL_HEX8_ARRAY(sets,f,7);
    TEST_ASSERT_EQUAL_size_t(7, AT_BuildSimple(AT_CMD_STOP,f));     TEST_ASSERT_EQUAL_HEX8_ARRAY(stop,f,7);
    TEST_ASSERT_EQUAL_size_t(8, AT_BuildTemp(23,f));                TEST_ASSERT_EQUAL_HEX8_ARRAY(t23,f,8);
    TEST_ASSERT_EQUAL_size_t(14, AT_BuildStart(29,f));              TEST_ASSERT_EQUAL_HEX8_ARRAY(st29,f,14);
}
void test_settings_write_shape(void){
    uint8_t f[AT_MAX_FRAME];
    TEST_ASSERT_EQUAL_size_t(13, AT_BuildSettingsWrite(23,f));
    const uint8_t head[]={0xAA,0x03,0x06,0x00,0x02,0xFF,0xFF,0x02,0x17,0x01,0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(head,f,11);
}

static int feed(at_parser_t *p, const uint8_t *b, size_t n, at_frame_t *last){
    int got=0; for(size_t i=0;i<n;i++) if(AT_ParseByte(p,b[i],last)) got++; return got;
}
void test_parser_version_reply_with_garbage_and_resync(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0x00,0x55,0xAA,0xAA,0x04,0x05,0x00,0x06,0x02,0x01,0x03,0x05,0x03,0x51,0x6F};
    TEST_ASSERT_EQUAL_INT(1, feed(&p,s,sizeof s,&fr));
    TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fr.cmd);
    TEST_ASSERT_EQUAL_UINT8(5, fr.len);
    TEST_ASSERT_EQUAL_HEX8(0x02, fr.payload[0]);
}
void test_parser_rejects_bad_crc_then_recovers(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t bad[]={0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA5};
    const uint8_t good[]={0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA4};
    TEST_ASSERT_EQUAL_INT(0, feed(&p,bad,sizeof bad,&fr));
    TEST_ASSERT_EQUAL_UINT32(1, p.crc_fails);
    TEST_ASSERT_EQUAL_INT(1, feed(&p,good,sizeof good,&fr));
    TEST_ASSERT_EQUAL_HEX8(0x17, fr.payload[0]);
}
void test_parser_ignores_requests_and_malformed_transition(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0x03,0xAA,0x00,0x00,0x06,0x5E,0xBC,          /* captured transition */
                       0xAA,0x03,0x00,0x00,0x06,0x5E,0xBC};         /* a request (dir 03) */
    TEST_ASSERT_EQUAL_INT(0, feed(&p,s,sizeof s,&fr));
    TEST_ASSERT_EQUAL_UINT32(0, p.crc_fails);
}
void test_parser_oversize_length_resyncs(void){
    at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr;
    const uint8_t s[]={0xAA,0x04,0x40,0xAA,0x04,0x01,0x00,0x11,0x17,0x73,0xA4};
    TEST_ASSERT_EQUAL_INT(1, feed(&p,s,sizeof s,&fr));
}
void test_phase_hint_table(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_OFF,      AT_PhaseHint(0,1));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_STARTING, AT_PhaseHint(1,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_STARTING, AT_PhaseHint(2,2));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_RUNNING,  AT_PhaseHint(2,4));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_RUNNING,  AT_PhaseHint(3,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_COOLDOWN, AT_PhaseHint(4,0));
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_FAULT,    AT_PhaseHint(9,0));
}

/* Replay the 712-reply bench capture: every frame parses, decodes, and the
   phase-hint timeline is exactly OFF -> STARTING -> RUNNING -> COOLDOWN -> OFF. */
static int hexval(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return -1; }
void test_capture_replay_timeline(void){
    FILE *fp=fopen("fixtures/autoterm_cycle.csv","r"); TEST_ASSERT_NOT_NULL(fp);
    char line[512]; at_parser_t p; memset(&p,0,sizeof p); at_frame_t fr; at_status_t st;
    uint8_t timeline[16]; int tl=0, frames=0; uint16_t first_mv=0;
    TEST_ASSERT_NOT_NULL(fgets(line,sizeof line,fp));            /* header */
    while(fgets(line,sizeof line,fp)){
        char *hex=strrchr(line,','); TEST_ASSERT_NOT_NULL(hex);
        int hi=-1;
        for(char *c=hex+1; *c; c++){
            int v=hexval(*c); if(v<0) continue;
            if(hi<0){ hi=v; continue; }
            if(AT_ParseByte(&p,(uint8_t)((hi<<4)|v),&fr)){
                TEST_ASSERT_TRUE(AT_DecodeStatus(&fr,&st));
                if(frames==0) first_mv=st.supply_mv;
                frames++;
                uint8_t h=AT_PhaseHint(st.major,st.minor);
                if(tl==0 || timeline[tl-1]!=h){ TEST_ASSERT_LESS_THAN(16,tl); timeline[tl++]=h; }
            }
            hi=-1;
        }
    }
    fclose(fp);
    TEST_ASSERT_EQUAL_INT(712, frames);
    TEST_ASSERT_EQUAL_UINT32(0, p.crc_fails);
    TEST_ASSERT_EQUAL_UINT16(12600, first_mv);
    const uint8_t want[]={HTR_HINT_OFF,HTR_HINT_STARTING,HTR_HINT_RUNNING,HTR_HINT_COOLDOWN,HTR_HINT_OFF};
    TEST_ASSERT_EQUAL_INT(5, tl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, timeline, 5);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_builders_byte_exact_vs_captures);
    RUN_TEST(test_settings_write_shape);
    RUN_TEST(test_parser_version_reply_with_garbage_and_resync);
    RUN_TEST(test_parser_rejects_bad_crc_then_recovers);
    RUN_TEST(test_parser_ignores_requests_and_malformed_transition);
    RUN_TEST(test_parser_oversize_length_resyncs);
    RUN_TEST(test_phase_hint_table);
    RUN_TEST(test_capture_replay_timeline);
    return UNITY_END(); }
