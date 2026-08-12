#include "unity.h"
#include "fake_nor.h"
#include "nvm_record.h"
#include <string.h>

static nvm_backend_t be;

void setUp(void) { fake_nor_init(&be); }
void tearDown(void) {}

static void fill(uint8_t *p, uint8_t base) {
    for (unsigned i = 0; i < NVM_PARAM_SIZE; i++)
        p[i] = (uint8_t)(base + i);
}

static void test_roundtrip(void) {
    uint8_t in[NVM_PARAM_SIZE], out[NVM_PARAM_SIZE];
    fill(in, 7);
    TEST_ASSERT_EQUAL_INT(0, nvm_record_write(&be, 0, 42u, in));
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(nvm_record_read(&be, 0, &seq, out));
    TEST_ASSERT_EQUAL_UINT32(42u, seq);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, NVM_PARAM_SIZE);
}

static void test_blank_is_invalid(void) {
    uint32_t seq;
    uint8_t out[NVM_PARAM_SIZE];
    TEST_ASSERT_FALSE(nvm_record_read(&be, 0, &seq, out));   /* erased 0xFF → no magic */
}

static void test_corrupt_payload_fails_crc(void) {
    uint8_t in[NVM_PARAM_SIZE];
    fill(in, 1);
    nvm_record_write(&be, 0, 5u, in);
    fake_nor_raw()[NVM_HEADER_SIZE + 128] &= 0x7F;           /* clear a bit in payload (NOR can only clear) */
    uint32_t seq;
    uint8_t out[NVM_PARAM_SIZE];
    TEST_ASSERT_FALSE(nvm_record_read(&be, 0, &seq, out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_blank_is_invalid);
    RUN_TEST(test_corrupt_payload_fails_crc);
    return UNITY_END();
}
