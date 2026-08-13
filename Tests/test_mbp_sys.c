#include "unity.h"
#include "mbp_sys.h"
#include "mb_regmodel.h"

static int s_reset_calls;
static void fake_reset(void) { s_reset_calls++; }

void setUp(void) { mb_reg_reset(); s_reset_calls = 0; mbp_sys_register(fake_reset); }
void tearDown(void) {}

static void test_relay_fw_is_read_only_constant(void) {
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(39, &o));
    TEST_ASSERT_EQUAL_UINT16(MB_RELAY_FW_VERSION, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(39, 1));
}

static void test_display_fw_rw(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(40, 250));
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(40, &o));
    TEST_ASSERT_EQUAL_UINT16(250, o);
}

static void test_reset_request_invokes_callback(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(34, 1));
    TEST_ASSERT_EQUAL_INT(1, s_reset_calls);
}

static void test_boot_flag_reg35_rw(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(35, 0xA5));
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(35, &o));
    TEST_ASSERT_EQUAL_UINT16(0xA5, o);
}

static void test_reset_reg34_read_returns_zero_and_no_callback(void) {
    uint16_t o = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(34, &o));
    TEST_ASSERT_EQUAL_UINT16(0, o);              /* read returns 0 */
    TEST_ASSERT_EQUAL_INT(0, s_reset_calls);     /* a READ must NOT invoke the reset callback */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_relay_fw_is_read_only_constant);
    RUN_TEST(test_display_fw_rw);
    RUN_TEST(test_reset_request_invokes_callback);
    RUN_TEST(test_boot_flag_reg35_rw);
    RUN_TEST(test_reset_reg34_read_returns_zero_and_no_callback);
    return UNITY_END();
}
