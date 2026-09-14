#include "unity.h"
#include "bl_session.h"
#include "bl_proto.h"
#include "bootcfg.h"
#include "crc32.h"
#include "fake_iflash.h"
#include <string.h>

static const iflash_backend_t *FL;
static bootcfg_t CFG;
static bl_session_t S;

static void seed_cfg_active_A(void) {
    memset(&CFG,0,sizeof CFG); CFG.magic=BOOTCFG_MAGIC; CFG.version=BOOTCFG_VERSION;
    CFG.active_slot=SLOT_A; CFG.slot[SLOT_A].state=SLOT_STATE_COMMITTED;
    CFG.slot[SLOT_A].length=8; CFG.slot[SLOT_A].crc32=crc32_compute(fake_iflash_ptr(SLOT_A_BASE),8);
}
void setUp(void){ fake_iflash_reset(); FL=fake_iflash_backend(); seed_cfg_active_A();
                  bl_session_init(&S, FL, &CFG); }
void tearDown(void){}

static uint16_t call(const uint8_t *req, uint16_t n, uint8_t *resp){ return bl_session_process(&S,req,n,resp); }

void test_target_is_inactive_slot(void){ TEST_ASSERT_EQUAL_INT(SLOT_B, S.target);
    TEST_ASSERT_EQUAL_UINT32(SLOT_B_BASE, S.target_base); }

void test_info(void){
    uint8_t req[]={1,BL_FC_CONTROL,BL_SUB_INFO}; uint8_t r[32];
    uint16_t n=call(req,sizeof req,r);
    TEST_ASSERT_EQUAL_HEX8(BL_SUB_INFO,r[2]); TEST_ASSERT_EQUAL_HEX8(SLOT_B,r[4]);
    TEST_ASSERT_GREATER_THAN_UINT16(0,n);
}
void test_data_before_erase_naks(void){
    uint8_t d[15]={1,BL_FC_DATA,0,0,0,0,8, 1,2,3,4,5,6,7,8}; uint8_t r[16];
    uint16_t n=call(d,sizeof d,r);
    TEST_ASSERT_EQUAL_HEX8(0xC2,r[1]); TEST_ASSERT_EQUAL_HEX8(BL_ERR_STATE,r[2]); (void)n;
}
void test_happy_path_erase_write_verify_commit(void){
    uint8_t r[64];
    /* ERASE */
    uint8_t er[]={1,BL_FC_CONTROL,BL_SUB_ERASE}; call(er,sizeof er,r);
    TEST_ASSERT_EQUAL_HEX8(0x00,r[3]);  /* ACK */
    TEST_ASSERT_EQUAL_INT(BL_ST_ERASED,S.state);
    /* WRITE 16 bytes at offset 0 */
    uint8_t payload[16]; for(int i=0;i<16;i++) payload[i]=(uint8_t)(i+1);
    uint8_t d[7+16]; d[0]=1;d[1]=BL_FC_DATA;d[2]=d[3]=d[4]=d[5]=0;d[6]=16; memcpy(&d[7],payload,16);
    call(d,sizeof d,r);
    TEST_ASSERT_EQUAL_HEX8(BL_FC_DATA,r[1]); /* data-ack echoes offset 0 */
    TEST_ASSERT_EQUAL_MEMORY(payload, fake_iflash_ptr(SLOT_B_BASE), 16);
    /* VERIFY */
    uint32_t crc=crc32_compute(payload,16);
    uint8_t v[3+8]={1,BL_FC_CONTROL,BL_SUB_VERIFY, 0,0,0,16, (uint8_t)(crc>>24),(uint8_t)(crc>>16),(uint8_t)(crc>>8),(uint8_t)crc};
    call(v,sizeof v,r);
    TEST_ASSERT_EQUAL_HEX8(0x00,r[3]); TEST_ASSERT_EQUAL_INT(BL_ST_VERIFIED,S.state);
    /* COMMIT */
    uint8_t c[]={1,BL_FC_CONTROL,BL_SUB_COMMIT}; call(c,sizeof c,r);
    TEST_ASSERT_EQUAL_HEX8(0x00,r[3]);
    TEST_ASSERT_EQUAL_INT(1,S.want_reset);
    bootcfg_t after; bootcfg_load(FL,&after);
    TEST_ASSERT_EQUAL_UINT16(SLOT_B, after.active_slot);
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_TRIAL, after.slot[SLOT_B].state);
    TEST_ASSERT_EQUAL_UINT32(16u, after.slot[SLOT_B].length);
}
void test_verify_bad_crc_naks(void){
    uint8_t r[64];
    uint8_t er[]={1,BL_FC_CONTROL,BL_SUB_ERASE}; call(er,sizeof er,r);
    uint8_t v[3+8]={1,BL_FC_CONTROL,BL_SUB_VERIFY, 0,0,0,8, 0,0,0,0}; /* wrong crc for 8 x 0xFF */
    call(v,sizeof v,r);
    TEST_ASSERT_EQUAL_HEX8(0xC1,r[1]); TEST_ASSERT_EQUAL_HEX8(BL_ERR_CRC,r[2]);
}
void test_erase_flash_fault_naks(void){
    uint8_t r[16]; fake_iflash_fail_next_erase();
    uint8_t er[]={1,BL_FC_CONTROL,BL_SUB_ERASE}; call(er,sizeof er,r);
    TEST_ASSERT_EQUAL_HEX8(0xC1,r[1]); TEST_ASSERT_EQUAL_HEX8(BL_ERR_FLASH,r[2]);
}
/* A slot stream must NEVER erase the bootloader region (pages 0-15 @
   0x08000000). Bench 2026-09-14: the boot path was left unbootable after an
   OTA attempt; this locks the invariant that erase_target's page range stays
   within the target slot and never reaches the bootloader. */
void test_erase_leaves_bootloader_region_intact(void){
    uint8_t *bl = fake_iflash_ptr(0x08000000u);
    for (int i = 0; i < 64; i++) bl[i] = (uint8_t)(0xA5 ^ i);
    uint8_t saved[64]; memcpy(saved, bl, sizeof saved);
    uint8_t er[]={1,BL_FC_CONTROL,BL_SUB_ERASE}; uint8_t r[16];
    call(er,sizeof er,r);
    TEST_ASSERT_EQUAL_HEX8(0x00,r[3]);              /* erase ACK */
    TEST_ASSERT_EQUAL_INT(BL_ST_ERASED,S.state);
    TEST_ASSERT_EQUAL_MEMORY(saved, bl, sizeof saved);  /* bootloader untouched */
}

int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_target_is_inactive_slot); RUN_TEST(test_info);
    RUN_TEST(test_data_before_erase_naks); RUN_TEST(test_happy_path_erase_write_verify_commit);
    RUN_TEST(test_verify_bad_crc_naks); RUN_TEST(test_erase_flash_fault_naks);
    RUN_TEST(test_erase_leaves_bootloader_region_intact);
    return UNITY_END();
}
