#include "unity.h"
#include "mbp_rgb.h"
#include "rgb.h"
#include "mb_regmodel.h"
#include "modbus_defs.h"
#include "fake_is31.h"
#include "mb_engine.h"
#include "modbus_crc.h"
#include <string.h>

static fake_is31_t f;

void setUp(void)
{
    fake_is31_init(&f);
    rgb_init(&f.api, 0U);
    rgb_process(0U);
    mb_reg_reset();
    mb_engine_init();
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

static void test_single_channel_write_keeps_others(void)
{
    uint8_t q[3];
    write_rgb(10U, 20U, 30U);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(77U, 99U));
    rgb_get_requested(q);
    TEST_ASSERT_EQUAL_UINT8(10U, q[0]);
    TEST_ASSERT_EQUAL_UINT8(99U, q[1]);
    TEST_ASSERT_EQUAL_UINT8(30U, q[2]);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(78U, 1U));
    rgb_get_requested(q);
    TEST_ASSERT_EQUAL_UINT8(10U, q[0]);
    TEST_ASSERT_EQUAL_UINT8(99U, q[1]);
    TEST_ASSERT_EQUAL_UINT8(1U, q[2]);
}

static void put_crc(uint8_t *b, uint16_t n)
{
    uint16_t c = modbus_crc16(b, n);
    b[n] = (uint8_t)c;
    b[n + 1U] = (uint8_t)(c >> 8);
}

/* A real FC16 frame (wire 75..77 = regs 76..78) -> one PWM latch with all three values. */
static void test_fc16_frame_one_latch(void)
{
    uint8_t req[32] = { MB_SLAVE_ADDR, MB_FC_WRITE_MULTIPLE, 0x00, 0x4B, 0x00, 0x03, 0x06,
                        0x00, 0xFF, 0x00, 0x80, 0x00, 0x01 };   /* r=255 g=128 b=1 */
    uint8_t resp[MB_MAX_FRAME];
    uint16_t rl = 0U;
    uint8_t a[3];
    unsigned before = f.pwm_latches;
    put_crc(req, 13U);
    mb_engine_process(req, 15U, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_WRITE_MULTIPLE, resp[1]);
    TEST_ASSERT_EQUAL_UINT16(8U, rl);
    rgb_process(10U);
    TEST_ASSERT_EQUAL_UINT(before + 1U, f.pwm_latches);
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(255U, a[0]);
    TEST_ASSERT_EQUAL_UINT8(128U, a[1]);
    TEST_ASSERT_EQUAL_UINT8(1U, a[2]);
    TEST_ASSERT_EQUAL_HEX8(0x08, f.regs[0x19]);   /* green 128 -> OUT1 0x808 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.regs[0x1B]);   /* red 255 -> OUT2 0xFFF */
    TEST_ASSERT_EQUAL_HEX8(0x10, f.regs[0x1D]);   /* blue 1 -> OUT3 16 */
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
    TEST_ASSERT_EQUAL_UINT(RGB_RETRY_LOG_END, MB_REG_MAX);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(MB_REG_MAX + 1U, &v));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(90U, &v));
    TEST_ASSERT_EQUAL_UINT16(rgb_get_diagnostic(90), v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(84U, 1));
}

static void test_retry_ring_reads_through_modbus_frame(void)
{
    const uint16_t address = RGB_RETRY_LOG_BASE - 1u + 64u;
    uint8_t req[8] = {MB_SLAVE_ADDR, MB_FC_READ_HOLDING,
                      (uint8_t)(address >> 8), (uint8_t)address, 0, 64};
    uint8_t response[MB_MAX_FRAME];
    uint16_t length = 0;
    put_crc(req, 6u);
    mb_engine_process(req, sizeof(req), response, &length);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING, response[1]);
    TEST_ASSERT_EQUAL_UINT8(128u, response[2]);
    TEST_ASSERT_EQUAL_UINT16(133u, length);
}

static void test_pwm_selector_sets_staged_wake_and_requires_off(void)
{
    uint16_t value;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(155u, &value));
    TEST_ASSERT_EQUAL_UINT16(400u, value);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(155u, 300u));
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(155u, 200u));
    f.mode_on_enable = 1u;
    write_rgb(255, 255, 0);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(155u, 400u));
    rgb_process(10);
    TEST_ASSERT_EQUAL_HEX8(0x31, f.regs[1]);
    TEST_ASSERT_EQUAL_HEX16(0x01, rgb_get_diagnostic(1512));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(155u, 400u));
    write_rgb(0, 0, 0);
    rgb_process(20);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(155u, 400u));
    write_rgb(255, 255, 0);
    rgb_process(30);
    TEST_ASSERT_EQUAL_HEX8(0x35, f.regs[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_selector_sets_staged_wake_and_requires_off);
    RUN_TEST(test_retry_ring_reads_through_modbus_frame);
    RUN_TEST(test_write_channels_set_requested);
    RUN_TEST(test_read_returns_applied);
    RUN_TEST(test_three_writes_one_update);
    RUN_TEST(test_single_channel_write_keeps_others);
    RUN_TEST(test_fc16_frame_one_latch);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_status_read_only);
    RUN_TEST(test_past_max_unbound);
    return UNITY_END();
}
