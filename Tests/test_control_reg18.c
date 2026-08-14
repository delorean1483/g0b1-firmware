#include "unity.h"
#include "control.h"
#include "mb_regmodel.h"

static apu_ctx_t ctx;
void setUp(void) { mb_reg_reset(); control_init(&ctx); control_regs_register(&ctx); }
void tearDown(void) {}

static void test_reg18_write_dismissed(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(18, OIL_WARNING_DISMISSED));
    TEST_ASSERT_EQUAL_UINT8(OIL_WARNING_DISMISSED, ctx.oil_change_state);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(18, &o));
    TEST_ASSERT_EQUAL_UINT16(OIL_WARNING_DISMISSED, o);
}

static void test_reg18_write_out_of_range_rejected(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(18, OIL_WARNING_DISMISSED + 1));
    TEST_ASSERT_EQUAL_UINT8(OIL_GOOD, ctx.oil_change_state);   /* unchanged from init */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reg18_write_dismissed);
    RUN_TEST(test_reg18_write_out_of_range_rejected);
    return UNITY_END();
}
