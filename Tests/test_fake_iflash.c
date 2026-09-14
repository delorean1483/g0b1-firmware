#include "unity.h"
#include "iflash_backend.h"
#include "fake_iflash.h"
#include <string.h>

void setUp(void) { fake_iflash_reset(); }
void tearDown(void) {}

void test_erased_is_ff(void) {
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t b[8]; TEST_ASSERT_EQUAL_INT(0, fl->read(fl->ctx, 0x08008000u, b, 8));
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, b[i]);
}
void test_program_then_read(void) {
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t w[8] = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
    TEST_ASSERT_EQUAL_INT(0, fl->program(fl->ctx, 0x08008000u, w, 8));
    uint8_t r[8]; fl->read(fl->ctx, 0x08008000u, r, 8);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(w, r, 8);
}
void test_program_only_on_erased(void) { /* second program to same dword must fail (bits already set) */
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t w[8] = {0,0,0,0,0,0,0,0};
    TEST_ASSERT_EQUAL_INT(0, fl->program(fl->ctx, 0x08008000u, w, 8));
    TEST_ASSERT_NOT_EQUAL(0, fl->program(fl->ctx, 0x08008000u, w, 8));
}
void test_erase_restores_ff(void) {
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t w[8] = {0}; fl->program(fl->ctx, 0x08008000u, w, 8);
    TEST_ASSERT_EQUAL_INT(0, fl->erase_page(fl->ctx, 16u)); /* page 16 = 0x08008000 */
    uint8_t r[8]; fl->read(fl->ctx, 0x08008000u, r, 8);
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, r[i]);
}
void test_program_rejects_unaligned_len(void) {
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t w[7] = {0};
    TEST_ASSERT_NOT_EQUAL(0, fl->program(fl->ctx, 0x08008000u, w, 7)); /* not multiple of 8 */
}
void test_fault_injection(void) {
    const iflash_backend_t *fl = fake_iflash_backend();
    uint8_t w[8] = {0};
    fake_iflash_fail_next_program();
    TEST_ASSERT_NOT_EQUAL(0, fl->program(fl->ctx, 0x08008000u, w, 8));
    TEST_ASSERT_EQUAL_INT(0, fl->program(fl->ctx, 0x08008000u, w, 8)); /* only the next one failed */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_erased_is_ff); RUN_TEST(test_program_then_read);
    RUN_TEST(test_program_only_on_erased); RUN_TEST(test_erase_restores_ff);
    RUN_TEST(test_program_rejects_unaligned_len); RUN_TEST(test_fault_injection);
    return UNITY_END();
}
