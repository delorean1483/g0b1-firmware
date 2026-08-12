#include "unity.h"
#include "fake_nor.h"
static nvm_backend_t be;
void setUp(void) { fake_nor_init(&be); }
void tearDown(void) {}

static void test_starts_erased(void) {
    uint8_t b[4]; TEST_ASSERT_EQUAL_INT(0, be.read(be.ctx, 0, b, 4));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, b, 4);
    uint8_t last; TEST_ASSERT_EQUAL_INT(0, be.read(be.ctx, fake_nor_size() - 1u, &last, 1));
    TEST_ASSERT_EQUAL_HEX8(0xFF, last);
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
static void test_erase_is_sector_scoped(void) {
    uint8_t z = 0x00;
    be.program(be.ctx, 0, &z, 1);            /* mark a byte in sector 0 */
    be.program(be.ctx, 5000, &z, 1);         /* mark a byte in sector 1 (4096..8191) */
    TEST_ASSERT_EQUAL_INT(0, be.erase(be.ctx, 1));   /* erase ONLY sector 1 */
    uint8_t r1; be.read(be.ctx, 5000, &r1, 1);
    TEST_ASSERT_EQUAL_HEX8(0xFF, r1);        /* sector 1 byte restored */
    uint8_t r0; be.read(be.ctx, 0, &r0, 1);
    TEST_ASSERT_EQUAL_HEX8(0x00, r0);        /* sector 0 byte SURVIVES the erase */
}
static void test_program_out_of_bounds_errors(void) {
    uint8_t b = 0x00;
    TEST_ASSERT_NOT_EQUAL(0, be.program(be.ctx, fake_nor_size(), &b, 1));
    TEST_ASSERT_NOT_EQUAL(0, be.program(be.ctx, fake_nor_size() - 1u, &b, 4)); /* addr+len overruns */
}
static void test_out_of_bounds_errors(void) {
    uint8_t b; TEST_ASSERT_NOT_EQUAL(0, be.read(be.ctx, fake_nor_size(), &b, 1));
    TEST_ASSERT_NOT_EQUAL(0, be.erase(be.ctx, be.sector_count));
}
int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_starts_erased); RUN_TEST(test_program_then_read);
    RUN_TEST(test_program_is_bit_clear_only); RUN_TEST(test_erase_is_sector_scoped);
    RUN_TEST(test_program_out_of_bounds_errors); RUN_TEST(test_out_of_bounds_errors);
    return UNITY_END();
}
