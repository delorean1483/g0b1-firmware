#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"

static uint16_t s_regs[MB_REG_MAX + 1];
static modbus_exc_t rd_sh(uint16_t r, uint16_t *o) { *o = s_regs[r]; return MB_EXC_NONE; }
static modbus_exc_t wr_sh(uint16_t r, uint16_t v) { s_regs[r] = v; return MB_EXC_NONE; }

void setUp(void) {
    mb_reg_reset(); mb_engine_init();
    for (uint16_t r = 1; r <= MB_REG_MAX; r++) { s_regs[r] = 0; mb_reg_bind(r, rd_sh, wr_sh); }
}
void tearDown(void) {}

static void put_crc(uint8_t *b, uint16_t n) { uint16_t c = modbus_crc16(b, n); b[n]=(uint8_t)c; b[n+1]=(uint8_t)(c>>8); }

static void test_bus_and_slave_counters_on_valid_frame(void) {
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_EXCEPTION }; put_crc(req, 2);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 4, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(1, mb_engine_counter(0)); /* bus message */
    TEST_ASSERT_EQUAL_UINT16(1, mb_engine_counter(3)); /* slave message */
}

static void test_comm_err_counter_on_bad_crc(void) {
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_EXCEPTION }; put_crc(req, 2);
    req[3] ^= 0xFF;                                    /* corrupt CRC */
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 4, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(0, rl);
    TEST_ASSERT_EQUAL_UINT16(1, mb_engine_counter(1)); /* comm error */
    TEST_ASSERT_EQUAL_UINT16(0, mb_engine_counter(0));
}

static void test_exception_counter(void) {
    mb_reg_reset();                                   /* reg 1 now unbound */
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_HOLDING, 0, 0, 0, 1 }; put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT16(1, mb_engine_counter(2)); /* exception */
}

static void test_broadcast_write_no_response_but_applies(void) {
    uint8_t req[16] = { MB_BROADCAST_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x0C, 0x03, 0xE8 };
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 99;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(0, rl);                  /* no response to broadcast */
    TEST_ASSERT_EQUAL_UINT16(1000, s_regs[13]);       /* write applied */
    TEST_ASSERT_EQUAL_UINT16(1, mb_engine_counter(0));/* bus message counted */
    TEST_ASSERT_EQUAL_UINT16(0, mb_engine_counter(3));/* not a slave-addressed message */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bus_and_slave_counters_on_valid_frame);
    RUN_TEST(test_comm_err_counter_on_bad_crc);
    RUN_TEST(test_exception_counter);
    RUN_TEST(test_broadcast_write_no_response_but_applies);
    return UNITY_END();
}
