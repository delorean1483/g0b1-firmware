#include "unity.h"
#include "crc32.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Known-answer vectors for CRC-32/IEEE-802.3 (zlib crc32). */
void test_crc32_empty(void)  { TEST_ASSERT_EQUAL_HEX32(0x00000000u, crc32_compute((const uint8_t*)"", 0)); }
void test_crc32_check(void)  { /* "123456789" -> 0xCBF43926 (the standard check value) */
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_compute((const uint8_t*)"123456789", 9)); }
void test_crc32_ascii(void)  { TEST_ASSERT_EQUAL_HEX32(0x414FA339u, crc32_compute((const uint8_t*)"The quick brown fox jumps over the lazy dog", 43)); }
void test_crc32_incremental_matches_oneshot(void) {
    const uint8_t buf[8] = {0,1,2,3,4,5,6,7};
    uint32_t c = CRC32_INIT;
    c = crc32_update(c, buf, 3);
    c = crc32_update(c, buf + 3, 5);
    TEST_ASSERT_EQUAL_HEX32(crc32_compute(buf, 8) ^ 0xFFFFFFFFu, c); /* update() leaves pre-xorout state */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_check);
    RUN_TEST(test_crc32_ascii);
    RUN_TEST(test_crc32_incremental_matches_oneshot);
    return UNITY_END();
}
