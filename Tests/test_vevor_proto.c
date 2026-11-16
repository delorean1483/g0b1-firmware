#include "vevor_proto.h"
#include "fake_htr_uart.h"
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

/* Through the htr_uart seam: first process() sends a STOP poll; a valid
   56-byte reply decodes; a refused write counts a transport error. */
void test_process_over_uart_seam(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v;
    TEST_ASSERT_TRUE(VEVOR_Init(&v,&u.api,0U));
    VEVOR_Process(&v,0U);
    TEST_ASSERT_EQUAL_size_t(1,u.writes); TEST_ASSERT_EQUAL_size_t(16,u.write_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05,u.tx[9]);                     /* STOP state byte */
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[5]=3; r[6]=4; r[11]=126; r[55]=VEVOR_Checksum(r,55U);
    fake_uart_inject(&u,r,56); VEVOR_Process(&v,20U);
    TEST_ASSERT_EQUAL_UINT32(1,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT8(3,v.status.state); TEST_ASSERT_EQUAL_UINT16(12600,v.status.supply_mv);
    u.refuse_write=true; VEVOR_Process(&v,1000U);
    TEST_ASSERT_EQUAL_UINT32(1,v.transport_errors);
}
/* A frame whose 8-bit sum passes but whose fields are impossible (seen on
   the bench while the RX comparator oscillated: state 0x40, "6.4 V") is
   counted as a bad frame, not decoded. */
static void inject_frame(fake_uart_t *u, uint8_t state, uint8_t level, uint8_t supply){
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[2]=0x02; r[3]=0x33;
    r[5]=state; r[6]=level; r[11]=supply; r[55]=VEVOR_Checksum(r,55U);
    fake_uart_inject(u,r,56);
}
void test_implausible_frames_are_rejected(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v;
    VEVOR_Init(&v,&u.api,0U);
    inject_frame(&u,0x40,1,120); VEVOR_Process(&v,10U);       /* state */
    inject_frame(&u,3,1,64);     VEVOR_Process(&v,20U);       /* 6.4 V */
    inject_frame(&u,3,11,120);   VEVOR_Process(&v,30U);       /* level */
    TEST_ASSERT_EQUAL_UINT32(0,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(3,v.checksum_failures);
    TEST_ASSERT_FALSE(v.status.valid);
    inject_frame(&u,0,0,0);      VEVOR_Process(&v,40U);       /* standby: supply reads 0 */
    inject_frame(&u,3,10,121);   VEVOR_Process(&v,50U);
    TEST_ASSERT_EQUAL_UINT32(2,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT8(3,v.status.state);
}
/* Supply may be 16-bit [10..11] (byte [10] is 0 in every 12 V capture): a
   24 V heater above 25.5 V puts a small value in [11]. Don't reject it. */
void test_supply_floor_only_applies_while_byte_10_is_zero(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v;
    VEVOR_Init(&v,&u.api,0U);
    uint8_t r[56]={0}; r[0]=0xAA; r[1]=0x77; r[2]=0x02; r[3]=0x33;
    r[5]=3; r[6]=5; r[10]=0x01; r[11]=0x14;                  /* 0x0114 = 27.6 V */
    r[55]=VEVOR_Checksum(r,55U);
    fake_uart_inject(&u,r,56); VEVOR_Process(&v,10U);
    TEST_ASSERT_EQUAL_UINT32(1,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(0,v.checksum_failures);
}
/* A frame cut short by a glitch must not swallow the start of the next one:
   a gap in the middle of a frame drops the partial frame. */
void test_partial_frame_resyncs_after_a_gap(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v;
    VEVOR_Init(&v,&u.api,0U);
    uint8_t part[22]={0}; part[0]=0xAA; part[1]=0x77;
    fake_uart_inject(&u,part,sizeof part); VEVOR_Process(&v,10U);
    VEVOR_Process(&v,20U); VEVOR_Process(&v,30U); VEVOR_Process(&v,40U); VEVOR_Process(&v,50U);
    inject_frame(&u,3,5,122); VEVOR_Process(&v,60U);
    TEST_ASSERT_EQUAL_UINT32(1,v.valid_frames);
    TEST_ASSERT_EQUAL_UINT8(5,v.status.level);
}
/* Every frame in both bench captures still decodes through the parser. */
static void feed_capture(const char *path, unsigned *frames, VEVOR_t *v, fake_uart_t *u){
    static uint8_t buf[200000]; size_t n=load_hex(path,buf,sizeof buf);
    *frames=0;
    for(size_t i=0;i+56<=n;){ if(buf[i]==0xAA && buf[i+1]==0x77){ (*frames)++; i+=56; } else i++; }
    uint32_t t=10U;   /* ~line rate: 4800 baud is ~4.8 bytes per 10 ms poll */
    for(size_t off=0; off<n; off+=5){
        size_t k = (n-off) < 5 ? (n-off) : 5;
        fake_uart_inject(u,&buf[off],k); VEVOR_Process(v,t); t+=10U;
    }
}
void test_capture_frames_all_decode(void){
    fake_uart_t u; fake_uart_init(&u); VEVOR_t v; unsigned f1,f2;
    VEVOR_Init(&v,&u.api,0U);
    feed_capture("fixtures/capture1.hex",&f1,&v,&u);
    feed_capture("fixtures/capture2.hex",&f2,&v,&u);
    TEST_ASSERT_EQUAL_UINT32(f1+f2, v.valid_frames);
    TEST_ASSERT_EQUAL_UINT32(0, v.checksum_failures);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_implausible_frames_are_rejected);
    RUN_TEST(test_partial_frame_resyncs_after_a_gap);
    RUN_TEST(test_supply_floor_only_applies_while_byte_10_is_zero);
    RUN_TEST(test_capture_frames_all_decode);
    RUN_TEST(test_build_frames_byte_exact);
    RUN_TEST(test_capture_checksums_all_pass);
    RUN_TEST(test_process_over_uart_seam); return UNITY_END(); }
