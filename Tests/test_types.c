#include "unity.h"
#include "types.h"
void setUp(void) {} void tearDown(void) {}
static void test_widths(void) {
    TEST_ASSERT_EQUAL_UINT(1, sizeof(UINT8));
    TEST_ASSERT_EQUAL_UINT(2, sizeof(UINT16));
    TEST_ASSERT_EQUAL_UINT(2, sizeof(INT16));
    TEST_ASSERT_EQUAL_UINT(4, sizeof(UINT32));
}
static void test_u16_wraps_at_16_bits(void) {
    UINT16 v = 0xFFFF; v = (UINT16)(v + 1);
    TEST_ASSERT_EQUAL_HEX16(0x0000, v);   /* would fail if UINT16 were 32-bit */
}
int main(void){ UNITY_BEGIN(); RUN_TEST(test_widths); RUN_TEST(test_u16_wraps_at_16_bits); return UNITY_END(); }
