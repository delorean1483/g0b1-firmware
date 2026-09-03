#include "unity.h"
#include "mbp_boot.h"
#include "mb_regmodel.h"
#include "modbus_defs.h"

static int g_engine; static int g_entered;
static int engine_running(void) { return g_engine; }
static void enter_bl(void) { g_entered = 1; }

void setUp(void) { mb_reg_reset(); g_engine = 0; g_entered = 0; mbp_boot_register(engine_running, enter_bl); }
void tearDown(void) {}

void test_enter_when_off(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(35, 0x00A5));
    TEST_ASSERT_EQUAL_INT(1, g_entered);
}
void test_refuse_when_engine_running(void) {
    g_engine = 1;
    TEST_ASSERT_EQUAL_INT(MB_EXC_SLAVE_DEVICE_FAILURE, mb_reg_write(35, 0x00A5));
    TEST_ASSERT_EQUAL_INT(0, g_entered);
}
void test_non_arm_value_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(35, 0x0001));
    TEST_ASSERT_EQUAL_INT(0, g_entered);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_when_off); RUN_TEST(test_refuse_when_engine_running);
    RUN_TEST(test_non_arm_value_is_noop);
    return UNITY_END();
}
