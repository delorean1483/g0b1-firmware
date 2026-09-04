#include "vevor_heater.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>
void setUp(void){} void tearDown(void){}

void test_build_frames_byte_exact(void){
    uint8_t f[16];
    const uint8_t start1[16]={0xAA,0x66,0x06,0x0B,0,0,0,0,1,0x06,0,0,0,0,0,0x18};
    const uint8_t run10[16] ={0xAA,0x66,0x02,0x0B,0,0,0,0,10,0x06,0x02,0,0,0,0,0x1F};
    const uint8_t stop1[16] ={0xAA,0x66,0x06,0x0B,0,0,0,0,1,0x05,0,0,0,0,0,0x17};
    VEVOR_BuildStartFrame(1U,f);  TEST_ASSERT_EQUAL_HEX8_ARRAY(start1,f,16);
    VEVOR_BuildRunFrame(10U,f);   TEST_ASSERT_EQUAL_HEX8_ARRAY(run10,f,16);
    VEVOR_BuildStopFrame(1U,f);   TEST_ASSERT_EQUAL_HEX8_ARRAY(stop1,f,16);
}

/* Read a .hex fixture, strip non-hex, decode to bytes. */
static size_t load_hex(const char *path, uint8_t *out, size_t cap){
    FILE *fp=fopen(path,"rb"); TEST_ASSERT_NOT_NULL(fp);
    size_t n=0; int hi=-1, c;
    while((c=fgetc(fp))!=EOF){
        int v; if(c>='0'&&c<='9')v=c-'0'; else if(c>='A'&&c<='F')v=c-'A'+10;
        else if(c>='a'&&c<='f')v=c-'a'+10; else continue;
        if(hi<0)hi=v; else { TEST_ASSERT_LESS_THAN(cap,n); out[n++]=(uint8_t)((hi<<4)|v); hi=-1; }
    }
    fclose(fp); return n;
}
/* Resync on AA 77, verify every 56-byte heater frame's checksum. */
static void check_capture(const char *path, int *frames, int *bad){
    static uint8_t buf[200000]; size_t n=load_hex(path,buf,sizeof buf);
    *frames=0; *bad=0;
    for(size_t i=0;i+56<=n;){
        if(buf[i]==0xAA && buf[i+1]==0x77){
            if(VEVOR_Checksum(&buf[i],55U)!=buf[i+55]) (*bad)++;
            (*frames)++; i+=56;
        } else i++;
    }
}
void test_capture_checksums_all_pass(void){
    int f1,b1,f2,b2;
    check_capture("fixtures/capture1.hex",&f1,&b1);
    check_capture("fixtures/capture2.hex",&f2,&b2);
    TEST_ASSERT_GREATER_THAN(0,f1+f2);
    TEST_ASSERT_EQUAL_INT(0,b1+b2);      /* handoff: all ~1752 responses valid */
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_build_frames_byte_exact);
    RUN_TEST(test_capture_checksums_all_pass); return UNITY_END(); }
