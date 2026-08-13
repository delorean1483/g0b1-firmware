#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"

static uint16_t s_regs[MB_REG_MAX + 1];
static modbus_exc_t rd_sh(uint16_t reg, uint16_t *out) { *out = s_regs[reg]; return MB_EXC_NONE; }
static modbus_exc_t wr_sh(uint16_t reg, uint16_t val) { s_regs[reg] = val; return MB_EXC_NONE; }
static modbus_exc_t wr_max2(uint16_t reg, uint16_t val) { if (val > 2) return MB_EXC_ILLEGAL_VALUE; s_regs[reg] = val; return MB_EXC_NONE; }

void setUp(void) {
    mb_reg_reset(); mb_engine_init();
    for (uint16_t r = 1; r <= MB_REG_MAX; r++) { s_regs[r] = 0; mb_reg_bind(r, rd_sh, wr_sh); }
}
void tearDown(void) {}

static void put_crc(uint8_t *b, uint16_t n) { uint16_t c = modbus_crc16(b, n); b[n] = (uint8_t)c; b[n+1] = (uint8_t)(c >> 8); }
static void assert_crc_ok(const uint8_t *f, uint16_t len) {
    uint16_t c = modbus_crc16(f, (uint16_t)(len - 2));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)c, f[len-2]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(c >> 8), f[len-1]);
}

static void test_write_single_echoes_request(void) {
    uint8_t req[16] = { MB_SLAVE_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x0C, 0x03, 0xE8 }; /* wire 12 -> reg 13, val 1000 */
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(req[i], resp[i]); /* echo */
    assert_crc_ok(resp, rl);
    TEST_ASSERT_EQUAL_UINT16(1000, s_regs[13]);
}

static void test_write_single_value_range_exception(void) {
    mb_reg_bind(19, rd_sh, wr_max2);
    uint8_t req[16] = { MB_SLAVE_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x12, 0x00, 0x05 }; /* wire 18 -> reg 19, val 5 */
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_WRITE_SINGLE | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_VALUE, resp[2]);
}

static void test_write_single_readonly_illegal_address(void) {
    mb_reg_bind(6, rd_sh, 0);                       /* read-only */
    uint8_t req[16] = { MB_SLAVE_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x05, 0x00, 0x01 };
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_ADDRESS, resp[2]);
}

static void test_write_multiple_two_regs(void) {
    /* wire start 12 -> regs 13,14; count 2; byte-count 4; values 1000, 70 */
    uint8_t req[32] = { MB_SLAVE_ADDR, MB_FC_WRITE_MULTIPLE, 0x00, 0x0C, 0x00, 0x02, 0x04,
                        0x03, 0xE8, 0x00, 0x46 };
    put_crc(req, 11);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 13, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);               /* addr fc starthi startlo cnthi cntlo + crc */
    TEST_ASSERT_EQUAL_UINT8(MB_FC_WRITE_MULTIPLE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, resp[2]); TEST_ASSERT_EQUAL_UINT8(0x0C, resp[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, resp[4]); TEST_ASSERT_EQUAL_UINT8(0x02, resp[5]);
    assert_crc_ok(resp, rl);
    TEST_ASSERT_EQUAL_UINT16(1000, s_regs[13]);
    TEST_ASSERT_EQUAL_UINT16(70, s_regs[14]);
}

static void test_write_single_short_frame_illegal_value(void) {
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x0C }; /* missing value bytes */
    uint16_t c = modbus_crc16(req, 4); req[4] = (uint8_t)c; req[5] = (uint8_t)(c >> 8);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 6, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_WRITE_SINGLE | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_VALUE, resp[2]);
}

static void test_write_multiple_short_frame_illegal_value(void) {
    /* claims count=2 (needs 4 data bytes) but frame carries none */
    uint8_t req[16] = { MB_SLAVE_ADDR, MB_FC_WRITE_MULTIPLE, 0x00, 0x0C, 0x00, 0x02, 0x04 };
    uint16_t c = modbus_crc16(req, 7); req[7] = (uint8_t)c; req[8] = (uint8_t)(c >> 8);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 9, resp, &rl);          /* 9 bytes: header(7)+crc(2), no data */
    TEST_ASSERT_EQUAL_UINT8(MB_FC_WRITE_MULTIPLE | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_VALUE, resp[2]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_single_echoes_request);
    RUN_TEST(test_write_single_value_range_exception);
    RUN_TEST(test_write_single_readonly_illegal_address);
    RUN_TEST(test_write_multiple_two_regs);
    RUN_TEST(test_write_single_short_frame_illegal_value);
    RUN_TEST(test_write_multiple_short_frame_illegal_value);
    return UNITY_END();
}
