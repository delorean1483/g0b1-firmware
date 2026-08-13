#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"
#include <string.h>

static uint16_t s_regs[MB_REG_MAX + 1];
static modbus_exc_t rd_shadow(uint16_t reg, uint16_t *out) { *out = s_regs[reg]; return MB_EXC_NONE; }

void setUp(void) {
    mb_reg_reset();
    mb_engine_init();
    for (uint16_t r = 1; r <= MB_REG_MAX; r++) { s_regs[r] = 0; mb_reg_bind(r, rd_shadow, 0); }
}
void tearDown(void) {}

/* Build [addr][fc][starthi][startlo][cnthi][cntlo] + CRC into buf; return length. */
static uint16_t build_req(uint8_t *buf, uint8_t fc, uint16_t start, uint16_t cnt) {
    buf[0] = MB_SLAVE_ADDR; buf[1] = fc;
    buf[2] = (uint8_t)(start >> 8); buf[3] = (uint8_t)start;
    buf[4] = (uint8_t)(cnt >> 8);   buf[5] = (uint8_t)cnt;
    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = (uint8_t)crc; buf[7] = (uint8_t)(crc >> 8);
    return 8;
}
static void assert_crc_ok(const uint8_t *f, uint16_t len) {
    uint16_t crc = modbus_crc16(f, (uint16_t)(len - 2));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)crc, f[len - 2]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(crc >> 8), f[len - 1]);
}

static void test_read_holding_two_regs(void) {
    s_regs[1] = 0x1234; s_regs[2] = 0x5678;
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 0, 2); /* wire start 0 -> regs 1,2 */
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(9, rl);                    /* addr fc bc + 4 data + 2 crc */
    TEST_ASSERT_EQUAL_UINT8(MB_SLAVE_ADDR, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(4, resp[2]);                /* byte count */
    TEST_ASSERT_EQUAL_UINT8(0x12, resp[3]); TEST_ASSERT_EQUAL_UINT8(0x34, resp[4]);
    TEST_ASSERT_EQUAL_UINT8(0x56, resp[5]); TEST_ASSERT_EQUAL_UINT8(0x78, resp[6]);
    assert_crc_ok(resp, rl);
}

static void test_read_input_shares_dispatch(void) {
    s_regs[6] = 0x0BEE;
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_req(req, MB_FC_READ_INPUT, 5, 1); /* wire 5 -> reg 6 */
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_INPUT, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(2, resp[2]);
    TEST_ASSERT_EQUAL_UINT8(0x0B, resp[3]); TEST_ASSERT_EQUAL_UINT8(0xEE, resp[4]);
    assert_crc_ok(resp, rl);
}

static void test_read_unbound_register_exception(void) {
    mb_reg_reset();                                     /* nothing bound now */
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 0, 1);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);                    /* addr fc|80 exc + crc */
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_ADDRESS, resp[2]);
    assert_crc_ok(resp, rl);
}

static void test_count_zero_is_illegal_value(void) {
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 0, 0);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_VALUE, resp[2]);
}

static void test_range_over_limit_illegal_address(void) {
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 52, 2); /* 52+2=54 > 53 */
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_ADDRESS, resp[2]);
}

static void test_bad_crc_no_response(void) {
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 99;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 0, 1);
    req[7] ^= 0xFF;                                     /* corrupt CRC */
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(0, rl);
}

static void test_wrong_address_no_response(void) {
    uint8_t req[16], resp[MB_MAX_FRAME]; uint16_t rl = 99;
    uint16_t n = build_req(req, MB_FC_READ_HOLDING, 0, 1);
    req[0] = 2; uint16_t crc = modbus_crc16(req, 6);    /* re-CRC for addr 2 */
    req[6] = (uint8_t)crc; req[7] = (uint8_t)(crc >> 8);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(0, rl);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_holding_two_regs);
    RUN_TEST(test_read_input_shares_dispatch);
    RUN_TEST(test_read_unbound_register_exception);
    RUN_TEST(test_count_zero_is_illegal_value);
    RUN_TEST(test_range_over_limit_illegal_address);
    RUN_TEST(test_bad_crc_no_response);
    RUN_TEST(test_wrong_address_no_response);
    return UNITY_END();
}
