#include "unity.h"
#include "fake_i2c.h"

static i2c_backend_t be;
void setUp(void) { fake_i2c_init(&be); }
void tearDown(void) {}

static void test_write_then_read_roundtrip(void) {
    uint8_t out[3] = {0x11, 0x22, 0x33};
    uint8_t in[3] = {0};
    TEST_ASSERT_EQUAL_INT(0, be.write(be.ctx, 0x04, out, 3));
    TEST_ASSERT_EQUAL_INT(0, be.read(be.ctx, 0x04, in, 3));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, in, 3);
}

static void test_raw_reflects_writes(void) {
    uint8_t v = 0x59;
    be.write(be.ctx, 0x06, &v, 1);
    TEST_ASSERT_EQUAL_UINT8(0x59, fake_i2c_raw()[0x06]);
}

static void test_st_bit_sets_oscrun(void) {
    uint8_t sec = 0x80; /* ST set */
    be.write(be.ctx, 0x00, &sec, 1);
    TEST_ASSERT_EQUAL_UINT8(0x20, fake_i2c_raw()[0x03] & 0x20); /* OSCRUN mirrored */
    sec = 0x00; /* ST clear */
    be.write(be.ctx, 0x00, &sec, 1);
    TEST_ASSERT_EQUAL_UINT8(0x00, fake_i2c_raw()[0x03] & 0x20);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_roundtrip);
    RUN_TEST(test_raw_reflects_writes);
    RUN_TEST(test_st_bit_sets_oscrun);
    return UNITY_END();
}
