#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"
#include <string.h>

void setUp(void) { mb_reg_reset(); mb_engine_init(); }
void tearDown(void) {}

static void put_crc(uint8_t *b, uint16_t n) { uint16_t c = modbus_crc16(b, n); b[n]=(uint8_t)c; b[n+1]=(uint8_t)(c>>8); }
static void assert_crc_ok(const uint8_t *f, uint16_t len) {
    uint16_t c = modbus_crc16(f, (uint16_t)(len-2));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)c, f[len-2]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(c>>8), f[len-1]);
}

static void test_read_exception_status(void) {
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_EXCEPTION }; put_crc(req, 2);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 4, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_EXCEPTION, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, resp[2]);
    assert_crc_ok(resp, rl);
}

static void test_report_slave_id(void) {
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_REPORT_SLAVE_ID }; put_crc(req, 2);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 4, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_REPORT_SLAVE_ID, resp[1]);
    uint8_t bc = resp[2];
    TEST_ASSERT_EQUAL_UINT8((uint8_t)strlen("EF-G0B1R"), bc);
    TEST_ASSERT_EQUAL_MEMORY("EF-G0B1R", &resp[3], bc);
    TEST_ASSERT_EQUAL_UINT16(3 + bc + 2, rl);
    assert_crc_ok(resp, rl);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_exception_status);
    RUN_TEST(test_report_slave_id);
    return UNITY_END();
}
