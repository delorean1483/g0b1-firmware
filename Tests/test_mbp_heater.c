#include "unity.h"
#include "mbp_heater.h"
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "mb_regmodel.h"
#include "fake_coproc_bus.h"
#include <string.h>

static fake_coproc_t c;
void setUp(void) {
    fake_coproc_init(&c);
    heater_init(&c.api, NULL, 0U);
    mb_reg_reset();
    mbp_heater_register();
}
void tearDown(void) {}

static void test_write_request_and_level(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(53U, 1U));
    TEST_ASSERT_EQUAL_UINT8(1U, heater_get_request());
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(54U, 7U));
    TEST_ASSERT_EQUAL_UINT8(7U, heater_get_level());
}
static void test_out_of_range_rejected(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 2U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 11U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 0U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(69U, 31U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(69U, 4U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(74U, 0x100U));
}
static void test_setpoint_and_type_select_rw(void) {
    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(69U, 24U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(69U, &v)); TEST_ASSERT_EQUAL_UINT16(24U, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(74U, 1U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(74U, &v)); TEST_ASSERT_EQUAL_UINT16(1U, v);
}
static void test_ro_regs_reject_writes(void) {
    const uint16_t ro[] = {55,56,57,58,59,60,61,62,63,64,65,66,67,68,70,71,72,73};
    for (unsigned i = 0; i < sizeof ro / sizeof ro[0]; i++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(ro[i], 1U), "RO reg accepted a write");
}
static void test_block_53_to_75_readable_and_past_max_out_of_range(void) {
    uint16_t v;
    for (uint16_t r = 53U; r <= 75U; r++) TEST_ASSERT_EQUAL_INT_MESSAGE(MB_EXC_NONE, mb_reg_read(r, &v), "reg not readable");
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(MB_REG_MAX + 1U, &v));
}
static void test_command_reg_75(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(75U, HTR_CMD_CLEAR_FAULT));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(75U, 0U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(75U, 6U));
}
static void test_telemetry_mirrors_coprocessor(void) {
    c.regs[HTR_R_PHASE] = HTR_PHASE_RUNNING; c.regs[HTR_R_ACTIVE_TYPE] = HTR_TYPE_VEVOR;
    c.regs[HTR_R_ACTIVE_LEVEL] = 4; c.regs[HTR_R_SUPPLY_MV] = 0x38; c.regs[HTR_R_SUPPLY_MV+1] = 0x31; /* 12600 */
    c.regs[HTR_R_VENDOR_STATE] = 3; c.regs[HTR_R_CAPS] = HTR_CAP_LEVEL;
    for (uint32_t t = 0; t <= 1500; t += 10) heater_process(t);
    uint16_t v;
    mb_reg_read(55U, &v); TEST_ASSERT_EQUAL_UINT16(3U, v);
    mb_reg_read(56U, &v); TEST_ASSERT_EQUAL_UINT16(4U, v);
    mb_reg_read(58U, &v); TEST_ASSERT_EQUAL_UINT16(12600U, v);
    mb_reg_read(68U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_TYPE_VEVOR, v);
    mb_reg_read(70U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_CAP_LEVEL, v);
    mb_reg_read(71U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_PHASE_RUNNING, v);
    mb_reg_read(72U, &v); TEST_ASSERT_EQUAL_HEX16(0x0300, v);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_request_and_level);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_setpoint_and_type_select_rw);
    RUN_TEST(test_ro_regs_reject_writes);
    RUN_TEST(test_block_53_to_75_readable_and_past_max_out_of_range);
    RUN_TEST(test_command_reg_75);
    RUN_TEST(test_telemetry_mirrors_coprocessor);
    return UNITY_END();
}
