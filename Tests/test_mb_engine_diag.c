#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"

void setUp(void) { mb_reg_reset(); mb_engine_init(); }
void tearDown(void) {}

/* Build [addr][0x08][subhi][sublo][datahi][datalo] + CRC. */
static uint16_t build_diag(uint8_t *b, uint16_t sub, uint16_t data) {
    b[0] = MB_SLAVE_ADDR; b[1] = MB_FC_DIAGNOSTICS;
    b[2] = (uint8_t)(sub >> 8); b[3] = (uint8_t)sub;
    b[4] = (uint8_t)(data >> 8); b[5] = (uint8_t)data;
    uint16_t c = modbus_crc16(b, 6); b[6] = (uint8_t)c; b[7] = (uint8_t)(c >> 8);
    return 8;
}
static void assert_crc_ok(const uint8_t *f, uint16_t len) {
    uint16_t c = modbus_crc16(f, (uint16_t)(len - 2));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)c, f[len-2]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(c >> 8), f[len-1]);
}

static void test_return_query_data_loopback(void) {
    uint8_t req[8], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_diag(req, MB_DIAG_RETURN_QUERY, 0xBEEF);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(req[i], resp[i]); /* echo */
    assert_crc_ok(resp, rl);
}

static void test_enter_test_mode_sets_flag(void) {
    TEST_ASSERT_FALSE(mb_engine_test_mode());
    uint8_t req[8], resp[MB_MAX_FRAME]; uint16_t rl = 0;
    uint16_t n = build_diag(req, MB_DIAG_ENTER_TEST, 0);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_TRUE(mb_engine_test_mode());
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8(req[i], resp[i]);
}

static void test_return_counter_subfunction(void) {
    /* Drive one valid frame so the bus-message counter (idx 0) increments. */
    uint8_t rd[8] = { MB_SLAVE_ADDR, MB_FC_READ_EXCEPTION }; uint16_t c = modbus_crc16(rd, 2);
    rd[2] = (uint8_t)c; rd[3] = (uint8_t)(c >> 8);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(rd, 4, resp, &rl);

    /* The diagnostics read itself also counts as a bus message, so its reported
       value must equal the LIVE counter after the call (not a pre-captured one). */
    uint8_t req[8]; uint16_t n = build_diag(req, MB_DIAG_FIRST_COUNTER, 0); /* 0x0B -> counter[0] */
    mb_engine_process(req, n, resp, &rl);
    uint16_t got = (uint16_t)(resp[4] << 8) | resp[5];
    TEST_ASSERT_GREATER_THAN_UINT16(0, got);
    TEST_ASSERT_EQUAL_UINT16(mb_engine_counter(0), got);
    assert_crc_ok(resp, rl);
}

static void test_clear_counters(void) {
    uint8_t rd[8] = { MB_SLAVE_ADDR, MB_FC_READ_EXCEPTION }; uint16_t c = modbus_crc16(rd, 2);
    rd[2] = (uint8_t)c; rd[3] = (uint8_t)(c >> 8);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(rd, 4, resp, &rl);
    TEST_ASSERT_GREATER_THAN_UINT16(0, mb_engine_counter(0));
    uint8_t req[8]; uint16_t n = build_diag(req, MB_DIAG_CLEAR_COUNTERS, 0);
    mb_engine_process(req, n, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(0, mb_engine_counter(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_return_query_data_loopback);
    RUN_TEST(test_enter_test_mode_sets_flag);
    RUN_TEST(test_return_counter_subfunction);
    RUN_TEST(test_clear_counters);
    return UNITY_END();
}
