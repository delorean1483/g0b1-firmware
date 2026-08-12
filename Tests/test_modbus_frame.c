#include "unity.h"
#include "modbus_crc.h"
#include "modbus_frame.h"
void setUp(void){} void tearDown(void){}

static void append_crc(uint8_t *f, uint16_t core_len) {
    uint16_t c = modbus_crc16(f, core_len);
    f[core_len] = (uint8_t)(c & 0xFF);
    f[core_len+1] = (uint8_t)(c >> 8);
}
static void test_ok(void) {
    uint8_t f[8] = {0x01,0x03,0x00,0x00,0x00,0x01,0,0};
    append_crc(f, 6);
    TEST_ASSERT_EQUAL(MB_FRAME_OK, mb_check_frame(f, 8, 0x01));
}
static void test_broadcast_ok(void) {
    uint8_t f[8] = {0x00,0x06,0x00,0x0A,0x00,0x01,0,0};
    append_crc(f, 6);
    TEST_ASSERT_EQUAL(MB_FRAME_OK, mb_check_frame(f, 8, 0x01));
}
static void test_too_short(void) {
    uint8_t f[3] = {0x01,0x03,0x00};
    TEST_ASSERT_EQUAL(MB_FRAME_TOO_SHORT, mb_check_frame(f, 3, 0x01));
}
static void test_not_for_us(void) {
    uint8_t f[8] = {0x02,0x03,0x00,0x00,0x00,0x01,0,0};
    append_crc(f, 6);
    TEST_ASSERT_EQUAL(MB_FRAME_NOT_FOR_US, mb_check_frame(f, 8, 0x01));
}
static void test_bad_crc(void) {
    uint8_t f[8] = {0x01,0x03,0x00,0x00,0x00,0x01,0xDE,0xAD};
    TEST_ASSERT_EQUAL(MB_FRAME_BAD_CRC, mb_check_frame(f, 8, 0x01));
}
int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_ok); RUN_TEST(test_broadcast_ok); RUN_TEST(test_too_short);
    RUN_TEST(test_not_for_us); RUN_TEST(test_bad_crc);
    return UNITY_END();
}
