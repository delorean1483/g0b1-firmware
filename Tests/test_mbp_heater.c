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
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 1U));
    uint16_t flags;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(64U, &flags));
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_REQUEST_AGAIN, flags);
    heater_note_cortex_traffic(0U);
    heater_process(0U);
    heater_process(250U);
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
    for (uint32_t t = 0; t <= 1500; t += 10) { heater_note_cortex_traffic(t); heater_process(t); }
    uint16_t v;
    mb_reg_read(55U, &v); TEST_ASSERT_EQUAL_UINT16(3U, v);
    mb_reg_read(56U, &v); TEST_ASSERT_EQUAL_UINT16(4U, v);
    mb_reg_read(58U, &v); TEST_ASSERT_EQUAL_UINT16(12600U, v);
    mb_reg_read(68U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_TYPE_VEVOR, v);
    mb_reg_read(70U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_CAP_LEVEL, v);
    mb_reg_read(71U, &v); TEST_ASSERT_EQUAL_UINT16(HTR_PHASE_RUNNING, v);
    mb_reg_read(72U, &v); TEST_ASSERT_EQUAL_HEX16(0x0300, v);
}
static modbus_exc_t active_maintenance(uint16_t reg, uint16_t *value)
{
    (void)reg;
    *value = 1;
    return MB_EXC_NONE;
}

static void test_maintenance_refuses_request_and_command(void)
{
    heater_note_cortex_traffic(0U);
    heater_process(0U);
    heater_process(250U);
    mb_reg_bind(MB_L011_UPDATE_MODE, active_maintenance, NULL);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(MB_HEATER_CONTROL_MODE, 1U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 1U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 0U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(75U, HTR_CMD_START));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(75U, HTR_CMD_STOP));
    TEST_ASSERT_EQUAL_UINT8(0U, heater_get_request());
    mb_reg_bind(MB_L011_UPDATE_MODE, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(53U, 1U));
    heater_forget_coproc();
    TEST_ASSERT_EQUAL_UINT8(0U, heater_get_request());
    heater_process(500U);
    heater_process(750U);
    TEST_ASSERT_EQUAL_UINT8(0U, c.regs[HTR_R_RUN_REQUEST]);
}

static void test_l011_version_identified_and_forgotten(void)
{
    uint16_t words[2];
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_VERSION, &words[0]));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_BUILD, &words[1]));
    TEST_ASSERT_EQUAL_UINT16(0U, words[0]);
    TEST_ASSERT_EQUAL_UINT16(0U, words[1]);
    c.regs[HTR_R_FW_MAJOR] = 1U;
    c.regs[HTR_R_FW_MINOR] = 2U;
    c.regs[HTR_R_FW_BUILD] = 0x34U;
    c.regs[HTR_R_FW_BUILD + 1U] = 0x12U;
    heater_note_cortex_traffic(0U);
    heater_process(0U);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_VERSION, &words[0]));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_BUILD, &words[1]));
    TEST_ASSERT_EQUAL_HEX16(0x0102U, words[0]);
    TEST_ASSERT_EQUAL_HEX16(0x1234U, words[1]);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(MB_L011_FW_VERSION, 0U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(MB_L011_FW_BUILD, 0U));

    heater_forget_coproc();
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_VERSION, &words[0]));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(MB_L011_FW_BUILD, &words[1]));
    TEST_ASSERT_EQUAL_UINT16(0U, words[0]);
    TEST_ASSERT_EQUAL_UINT16(0U, words[1]);
}

static void test_control_mode_capability_applied_readback_and_invalid_values(void)
{
    uint16_t mode=99;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE,mb_reg_read(MB_HEATER_CONTROL_MODE,&mode));
    TEST_ASSERT_EQUAL(0,mode);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE,mb_reg_write(MB_HEATER_CONTROL_MODE,1));
    c.regs[HTR_R_ACTIVE_TYPE]=HTR_TYPE_AUTOTERM;
    c.regs[HTR_R_CAPS+1]=(uint8_t)(HTR_CAP_POWER_MODE>>8);
    for(uint32_t t=0;t<=500;t+=250){heater_note_cortex_traffic(t);heater_process(t);}
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE,mb_reg_write(MB_HEATER_CONTROL_MODE,2));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE,mb_reg_write(MB_HEATER_CONTROL_MODE,1));
    mb_reg_read(MB_HEATER_CONTROL_MODE,&mode);TEST_ASSERT_EQUAL(0,mode);
    for(uint32_t t=750;t<=1000;t+=250){heater_note_cortex_traffic(t);heater_process(t);}
    mb_reg_read(MB_HEATER_CONTROL_MODE,&mode);TEST_ASSERT_EQUAL(1,mode);
    TEST_ASSERT_EQUAL(0,c.keyed_writes);
    c.regs[HTR_R_CAPS+1]=0;
    heater_note_cortex_traffic(1250);heater_process(1250);
    mb_reg_read(MB_HEATER_CONTROL_MODE,&mode);TEST_ASSERT_EQUAL(0,mode);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE,mb_reg_write(MB_HEATER_CONTROL_MODE,0));
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_control_mode_capability_applied_readback_and_invalid_values);
    RUN_TEST(test_l011_version_identified_and_forgotten);
    RUN_TEST(test_maintenance_refuses_request_and_command);
    RUN_TEST(test_write_request_and_level);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_setpoint_and_type_select_rw);
    RUN_TEST(test_ro_regs_reject_writes);
    RUN_TEST(test_block_53_to_75_readable_and_past_max_out_of_range);
    RUN_TEST(test_command_reg_75);
    RUN_TEST(test_telemetry_mirrors_coprocessor);
    return UNITY_END();
}
