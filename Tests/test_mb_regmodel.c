#include "unity.h"
#include "mb_regmodel.h"

void setUp(void) { mb_reg_reset(); }
void tearDown(void) {}

static uint16_t s_val;
static modbus_exc_t rd_ok(uint16_t reg, uint16_t *out) { (void)reg; *out = s_val; return MB_EXC_NONE; }
static modbus_exc_t wr_ok(uint16_t reg, uint16_t val) { (void)reg; s_val = val; return MB_EXC_NONE; }
static modbus_exc_t wr_range(uint16_t reg, uint16_t val) { (void)reg; return val <= 2 ? MB_EXC_NONE : MB_EXC_ILLEGAL_VALUE; }

static void test_unbound_reads_illegal_address(void) {
    uint16_t o = 0xAAAA;
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(10, &o));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(10, 5));
}

static void test_bound_rw_roundtrip(void) {
    mb_reg_bind(13, rd_ok, wr_ok);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(13, 1234));
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(13, &o));
    TEST_ASSERT_EQUAL_UINT16(1234, o);
}

static void test_readonly_write_is_illegal_address(void) {
    mb_reg_bind(6, rd_ok, 0);            /* read-only */
    uint16_t o; s_val = 77;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(6, &o));
    TEST_ASSERT_EQUAL_UINT16(77, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(6, 1));
}

static void test_provider_value_range(void) {
    mb_reg_bind(19, rd_ok, wr_range);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(19, 2));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(19, 3));
}

static void test_out_of_range_reg_illegal_address(void) {
    uint16_t o;
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(0, &o));   /* reg 0 invalid */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(53, &o));  /* > MB_REG_MAX */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_unbound_reads_illegal_address);
    RUN_TEST(test_bound_rw_roundtrip);
    RUN_TEST(test_readonly_write_is_illegal_address);
    RUN_TEST(test_provider_value_range);
    RUN_TEST(test_out_of_range_reg_illegal_address);
    return UNITY_END();
}
