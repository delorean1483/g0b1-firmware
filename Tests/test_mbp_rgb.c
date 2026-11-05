#include "unity.h"
#include "mbp_rgb.h"
#include "rgb.h"
#include "mb_regmodel.h"
#include "modbus_defs.h"
#include "fake_is31.h"

static fake_is31_t f;

void setUp(void)
{
    fake_is31_init(&f);
    rgb_init(&f.api, 0U);
    rgb_process(0U);
    mb_reg_reset();
    mbp_rgb_register();
}
void tearDown(void) {}

static void write_rgb(uint16_t r, uint16_t g, uint16_t b)
{
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(76U, r));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(77U, g));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(78U, b));
}

static void test_write_channels_set_requested(void)
{
    uint8_t q[3];
    write_rgb(10U, 20U, 30U);
    rgb_get_requested(q);
    TEST_ASSERT_EQUAL_UINT8(10U, q[0]);
    TEST_ASSERT_EQUAL_UINT8(20U, q[1]);
    TEST_ASSERT_EQUAL_UINT8(30U, q[2]);
}

static void test_read_returns_applied(void)
{
    uint16_t v;
    write_rgb(10U, 20U, 30U);
    for (uint16_t r = 76U; r <= 78U; r++) {
        TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(r, &v));
        TEST_ASSERT_EQUAL_UINT16(0U, v);
    }
    rgb_process(10U);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(76U, &v)); TEST_ASSERT_EQUAL_UINT16(10U, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(77U, &v)); TEST_ASSERT_EQUAL_UINT16(20U, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(78U, &v)); TEST_ASSERT_EQUAL_UINT16(30U, v);
}

static void test_three_writes_one_update(void)
{
    unsigned before = f.pwm_latches;
    write_rgb(10U, 20U, 30U);
    rgb_process(10U);
    TEST_ASSERT_EQUAL_UINT(before + 1U, f.pwm_latches);
}

static void test_out_of_range_rejected(void)
{
    uint8_t q[3];
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(77U, 5U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(77U, 256U));
    rgb_get_requested(q);
    TEST_ASSERT_EQUAL_UINT8(5U, q[1]);
}

static void test_status_read_only(void)
{
    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(79U, 1U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(79U, &v));
    TEST_ASSERT_EQUAL_UINT16(rgb_get_status(), v);
}

static void test_past_max_unbound(void)
{
    uint16_t v;
    TEST_ASSERT_EQUAL_UINT(79U, MB_REG_MAX);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(MB_REG_MAX + 1U, &v));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_channels_set_requested);
    RUN_TEST(test_read_returns_applied);
    RUN_TEST(test_three_writes_one_update);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_status_read_only);
    RUN_TEST(test_past_max_unbound);
    return UNITY_END();
}
