#include "unity.h"
#include "fake_nor.h"
static nvm_backend_t be;
void setUp(void) { fake_nor_init(&be); }
void tearDown(void) {}

static void test_starts_erased(void) {
    uint8_t b[4]; TEST_ASSERT_EQUAL_INT(0, be.read(be.ctx, 0, b, 4));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, b, 4);
}
static void test_program_then_read(void) {
    uint8_t w[3] = {0x11,0x22,0x33};
    TEST_ASSERT_EQUAL_INT(0, be.program(be.ctx, 10, w, 3));
    uint8_t r[3]; be.read(be.ctx, 10, r, 3);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(w, r, 3);
}
static void test_program_is_bit_clear_only(void) {           /* AND semantics */
    uint8_t a = 0xF0; be.program(be.ctx, 0, &a, 1);
    uint8_t b = 0x0F; be.program(be.ctx, 0, &b, 1);          /* 0xF0 & 0x0F */
    uint8_t r; be.read(be.ctx, 0, &r, 1);
    TEST_ASSERT_EQUAL_HEX8(0x00, r);
}
static void test_erase_restores_ff(void) {
    uint8_t a = 0x00; be.program(be.ctx, 5000, &a, 1);       /* sector 1 (4096..) */
    TEST_ASSERT_EQUAL_INT(0, be.erase(be.ctx, 1));
    uint8_t r; be.read(be.ctx, 5000, &r, 1);
    TEST_ASSERT_EQUAL_HEX8(0xFF, r);
    uint8_t s0; be.read(be.ctx, 0, &s0, 1);                  /* other sectors untouched: still 0xFF */
    TEST_ASSERT_EQUAL_HEX8(0xFF, s0);
}
static void test_out_of_bounds_errors(void) {
    uint8_t b; TEST_ASSERT_NOT_EQUAL(0, be.read(be.ctx, fake_nor_size(), &b, 1));
    TEST_ASSERT_NOT_EQUAL(0, be.erase(be.ctx, be.sector_count));
}
int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_starts_erased); RUN_TEST(test_program_then_read);
    RUN_TEST(test_program_is_bit_clear_only); RUN_TEST(test_erase_restores_ff);
    RUN_TEST(test_out_of_bounds_errors);
    return UNITY_END();
}
