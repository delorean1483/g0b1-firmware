#include "unity.h"
#include "bl_proto.h"
#include <string.h>

void setUp(void) {} void tearDown(void) {}

void test_parse_control_info(void) {
    uint8_t req[] = {1, BL_FC_CONTROL, BL_SUB_INFO};   /* CRC stripped by caller */
    bl_sub_t sub; uint32_t a, b;
    TEST_ASSERT_EQUAL_INT(0, bl_parse_control(req, sizeof req, &sub, &a, &b));
    TEST_ASSERT_EQUAL_INT(BL_SUB_INFO, sub);
}
void test_parse_control_verify_fields(void) {
    uint8_t req[] = {1, BL_FC_CONTROL, BL_SUB_VERIFY, 0,0,0x01,0x00, 0xDE,0xAD,0xBE,0xEF};
    bl_sub_t sub; uint32_t len, crc;
    TEST_ASSERT_EQUAL_INT(0, bl_parse_control(req, sizeof req, &sub, &len, &crc));
    TEST_ASSERT_EQUAL_INT(BL_SUB_VERIFY, sub);
    TEST_ASSERT_EQUAL_UINT32(0x00000100u, len);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, crc);
}
void test_parse_data(void) {
    uint8_t req[3 + 4 + 1 + 4];
    req[0]=1; req[1]=BL_FC_DATA;
    req[2]=0;req[3]=0;req[4]=0x10;req[5]=0x00;   /* offset 0x1000 BE */
    req[6]=4;                                    /* len */
    req[7]=0xA;req[8]=0xB;req[9]=0xC;req[10]=0xD;
    uint32_t off; const uint8_t *d; uint16_t dl;
    TEST_ASSERT_EQUAL_INT(0, bl_parse_data(req, sizeof req, &off, &d, &dl));
    TEST_ASSERT_EQUAL_UINT32(0x1000u, off);
    TEST_ASSERT_EQUAL_UINT16(4, dl);
    TEST_ASSERT_EQUAL_HEX8(0xA, d[0]); TEST_ASSERT_EQUAL_HEX8(0xD, d[3]);
}
void test_parse_data_len_overflow_rejected(void) {
    uint8_t req[3+4+1+2]; req[0]=1; req[1]=BL_FC_DATA;
    req[2]=req[3]=req[4]=req[5]=0; req[6]=10;    /* claims 10 but only 2 present */
    uint32_t off; const uint8_t *d; uint16_t dl;
    TEST_ASSERT_NOT_EQUAL(0, bl_parse_data(req, sizeof req, &off, &d, &dl));
}
void test_build_info(void) {
    bl_info_t i = { 1, 1, 0x38000u, BL_CHUNK_MAX, 1 };
    uint8_t resp[32];
    uint16_t n = bl_build_info(resp, &i);
    TEST_ASSERT_EQUAL_UINT16(12, n);             /* addr,fc,sub, ver, slot, size4, chunk2, algo */
    TEST_ASSERT_EQUAL_HEX8(1, resp[0]); TEST_ASSERT_EQUAL_HEX8(BL_FC_CONTROL, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(BL_SUB_INFO, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[5]); TEST_ASSERT_EQUAL_HEX8(0x03, resp[6]);
    TEST_ASSERT_EQUAL_HEX8(0x80, resp[7]); TEST_ASSERT_EQUAL_HEX8(0x00, resp[8]); /* 0x38000 BE */
    TEST_ASSERT_EQUAL_HEX8(1, resp[11]);         /* crc_algo */
}
void test_build_data_ack(void) {
    uint8_t resp[16];
    uint16_t n = bl_build_data_ack(resp, 0x1234u);
    TEST_ASSERT_EQUAL_UINT16(6, n);
    TEST_ASSERT_EQUAL_HEX8(BL_FC_DATA, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[4]); TEST_ASSERT_EQUAL_HEX8(0x34, resp[5]);
}
void test_build_nak(void) {
    uint8_t resp[8]; uint16_t n = bl_build_nak(resp, BL_FC_DATA, BL_ERR_CRC);
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_HEX8(0xC2, resp[1]); TEST_ASSERT_EQUAL_HEX8(BL_ERR_CRC, resp[2]);
}
void test_build_ack(void) {
    uint8_t resp[8];
    uint16_t n = bl_build_ack(resp, BL_FC_CONTROL, BL_SUB_ERASE);
    TEST_ASSERT_EQUAL_UINT16(4, n);
    TEST_ASSERT_EQUAL_HEX8(1, resp[0]); TEST_ASSERT_EQUAL_HEX8(BL_FC_CONTROL, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(BL_SUB_ERASE, resp[2]); TEST_ASSERT_EQUAL_HEX8(0x00, resp[3]);
}
void test_build_status(void) {
    uint8_t resp[16];
    uint16_t n = bl_build_status(resp, 1u, 0x00012345u);
    TEST_ASSERT_EQUAL_UINT16(8, n);
    TEST_ASSERT_EQUAL_HEX8(BL_FC_CONTROL, resp[1]); TEST_ASSERT_EQUAL_HEX8(BL_SUB_STATUS, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(1, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[4]); TEST_ASSERT_EQUAL_HEX8(0x01, resp[5]);
    TEST_ASSERT_EQUAL_HEX8(0x23, resp[6]); TEST_ASSERT_EQUAL_HEX8(0x45, resp[7]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_control_info); RUN_TEST(test_parse_control_verify_fields);
    RUN_TEST(test_parse_data); RUN_TEST(test_parse_data_len_overflow_rejected);
    RUN_TEST(test_build_info); RUN_TEST(test_build_data_ack); RUN_TEST(test_build_nak);
    RUN_TEST(test_build_ack); RUN_TEST(test_build_status);
    return UNITY_END();
}
