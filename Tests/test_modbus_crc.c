#include "unity.h"
#include <string.h>
#include "modbus_crc.h"
void setUp(void){} void tearDown(void){}

/* CRC-16/MODBUS catalog check value for ASCII "123456789" is 0x4B37. */
static void test_known_answer(void) {
    const uint8_t s[] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX16(0x4B37, modbus_crc16(s, sizeof(s)));
}
/* Property: CRC over (message + its own CRC appended low-byte-first) == 0. */
static void test_roundtrip_is_zero(void) {
    uint8_t f[8] = {0x01,0x03,0x00,0x00,0x00,0x01,0,0};
    uint16_t c = modbus_crc16(f, 6);
    f[6] = (uint8_t)(c & 0xFF);         /* CRC low byte first on the wire */
    f[7] = (uint8_t)(c >> 8);
    TEST_ASSERT_EQUAL_HEX16(0x0000, modbus_crc16(f, 8));
}
int main(void){ UNITY_BEGIN(); RUN_TEST(test_known_answer); RUN_TEST(test_roundtrip_is_zero); return UNITY_END(); }
