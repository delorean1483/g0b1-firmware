#include "unity.h"
#include "modbus_crc.h"
#include "modbus_read.h"
void setUp(void){} void tearDown(void){}

/* Mock register model: reg 1 -> 0x1234, reg 2 -> 0xABCD, others illegal. */
static bool mock_reader(uint16_t reg, uint16_t *out) {
    if (reg == 1) { *out = 0x1234; return true; }
    if (reg == 2) { *out = 0xABCD; return true; }
    return false;
}
static void test_reads_two_registers(void) {
    uint8_t r[16];
    uint16_t n = mb_build_read_holding(0x01, 1, 2, mock_reader, r);
    TEST_ASSERT_EQUAL_UINT16(9, n);           /* 3 hdr + 4 data + 2 crc */
    TEST_ASSERT_EQUAL_HEX8(0x01, r[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, r[1]);
    TEST_ASSERT_EQUAL_HEX8(0x04, r[2]);       /* byte count */
    TEST_ASSERT_EQUAL_HEX8(0x12, r[3]);       /* reg1 hi (big-endian) */
    TEST_ASSERT_EQUAL_HEX8(0x34, r[4]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, r[5]);       /* reg2 hi */
    TEST_ASSERT_EQUAL_HEX8(0xCD, r[6]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, modbus_crc16(r, n)); /* valid trailing CRC */
}
static void test_illegal_address_exception(void) {
    uint8_t r[16];
    uint16_t n = mb_build_read_holding(0x01, 1, 5, mock_reader, r); /* reg 3 illegal */
    TEST_ASSERT_EQUAL_UINT16(5, n);
    TEST_ASSERT_EQUAL_HEX8(0x83, r[1]);       /* func | 0x80 */
    TEST_ASSERT_EQUAL_HEX8(0x02, r[2]);       /* illegal data address */
    TEST_ASSERT_EQUAL_HEX16(0x0000, modbus_crc16(r, n));
}
static void test_count_over_max_exception(void) {
    uint8_t r[300];
    uint16_t n = mb_build_read_holding(0x01, 1, 200, mock_reader, r);
    TEST_ASSERT_EQUAL_UINT16(5, n);
    TEST_ASSERT_EQUAL_HEX8(0x83, r[1]);       /* func | 0x80 */
    TEST_ASSERT_EQUAL_HEX8(0x03, r[2]);       /* illegal data value */
    TEST_ASSERT_EQUAL_HEX16(0x0000, modbus_crc16(r, n));
}
static void test_count_zero_exception(void) {
    uint8_t r[16];
    uint16_t n = mb_build_read_holding(0x01, 1, 0, mock_reader, r);
    TEST_ASSERT_EQUAL_UINT16(5, n);
    TEST_ASSERT_EQUAL_HEX8(0x83, r[1]);       /* func | 0x80 */
    TEST_ASSERT_EQUAL_HEX8(0x03, r[2]);       /* illegal data value */
    TEST_ASSERT_EQUAL_HEX16(0x0000, modbus_crc16(r, n));
}
int main(void){ UNITY_BEGIN(); RUN_TEST(test_reads_two_registers); RUN_TEST(test_illegal_address_exception); RUN_TEST(test_count_over_max_exception); RUN_TEST(test_count_zero_exception); return UNITY_END(); }
