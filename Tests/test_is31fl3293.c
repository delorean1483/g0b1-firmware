#include "unity.h"
#include "is31fl3293.h"
#include "fake_is31.h"
#include <string.h>

static fake_is31_t f;
void setUp(void) { fake_is31_init(&f); }
void tearDown(void) {}

static void expect_xfer(unsigned i, uint8_t reg, uint8_t len, const uint8_t *data)
{
    TEST_ASSERT_EQUAL_UINT8(reg, f.log[i].reg);
    TEST_ASSERT_EQUAL_UINT8(len, f.log[i].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, f.log[i].data, len);
}

static void test_init_sequence(void)
{
    static const uint8_t a[] = {0xC5}, b[] = {0x70}, c[] = {0x00}, d[] = {0x3F};
    static const uint8_t e[] = {0xFF, 0xFF, 0xFF}, g[6] = {0}, h[] = {0x75};
    TEST_ASSERT_EQUAL_INT(0, is31_init(&f.api));
    TEST_ASSERT_EQUAL_UINT(9, f.nlog);
    expect_xfer(0, 0x3F, 1, a);
    expect_xfer(1, 0x01, 1, b);
    expect_xfer(2, 0x02, 1, c);
    expect_xfer(3, 0x03, 1, d);
    expect_xfer(4, 0x10, 3, e);
    expect_xfer(5, 0x27, 1, a);
    expect_xfer(6, 0x19, 6, g);
    expect_xfer(7, 0x28, 1, a);
    expect_xfer(8, 0x01, 1, h);
}
static void test_set_pwm_packs_and_latches_once(void)
{
    static const uint8_t p[] = {0xBC, 0x0A, 0xFF, 0x0F, 0x00, 0x00}, l[] = {0xC5};
    TEST_ASSERT_EQUAL_INT(0, is31_set_pwm(&f.api, 0x0ABC, 4095, 0));
    expect_xfer(0, 0x19, 6, p);
    expect_xfer(1, 0x28, 1, l);
    TEST_ASSERT_EQUAL_UINT(1, f.pwm_latches);
    TEST_ASSERT_EQUAL_UINT(2, f.nlog);
}
static void test_set_pwm_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(0, is31_set_pwm(&f.api, 5000, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0xFF, f.log[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0F, f.log[0].data[1]);
}
static void test_failure_propagates(void)
{
    f.fail = true;
    TEST_ASSERT_EQUAL_INT(-1, is31_init(&f.api));
    TEST_ASSERT_EQUAL_UINT(0, f.nlog);
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    TEST_ASSERT_EQUAL_INT(-1, is31_set_pwm(&f.api, 1, 2, 3));
}
static void test_restore_sequence(void)
{
    static const uint8_t c0[] = {0x00}, cur[] = {0x3F}, e[] = {0xFF, 0xFF, 0xFF};
    static const uint8_t k[] = {0xC5}, p[] = {0xBC, 0x0A, 0xFF, 0x0F, 0x00, 0x00}, on[] = {0x75};
    TEST_ASSERT_EQUAL_INT(0, is31_restore(&f.api, 0x0ABC, 4095, 0));
    TEST_ASSERT_EQUAL_UINT(7, f.nlog);
    expect_xfer(0, 0x02, 1, c0);
    expect_xfer(1, 0x03, 1, cur);
    expect_xfer(2, 0x10, 3, e);
    expect_xfer(3, 0x27, 1, k);
    expect_xfer(4, 0x19, 6, p);
    expect_xfer(5, 0x28, 1, k);
    expect_xfer(6, 0x01, 1, on);
    TEST_ASSERT_EQUAL_UINT(1, f.pwm_latches);
}
static void test_restore_clamps_and_stops_at_failure(void)
{
    TEST_ASSERT_EQUAL_INT(0, is31_restore(&f.api, 5000, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0xFF, f.log[4].data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0F, f.log[4].data[1]);
    fake_is31_init(&f);
    f.fail = true;
    TEST_ASSERT_EQUAL_INT(-1, is31_restore(&f.api, 1, 2, 3));
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
}
static void test_failed_pwm_write_sends_no_latch(void)
{
    f.fail_pwm = true;
    TEST_ASSERT_EQUAL_INT(-1, is31_set_pwm(&f.api, 1, 2, 3));
    TEST_ASSERT_EQUAL_UINT(0, f.nlog);
    TEST_ASSERT_EQUAL_UINT(0, f.pwm_latches);
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    fake_is31_init(&f);
    f.fail = true;
    TEST_ASSERT_EQUAL_INT(-1, is31_set_pwm(&f.api, 1, 2, 3));
    TEST_ASSERT_EQUAL_UINT(0, f.pwm_latches);
}
static void test_shutdown(void)
{
    static const uint8_t b[] = {0x70};
    TEST_ASSERT_EQUAL_INT(0, is31_shutdown(&f.api));
    expect_xfer(0, 0x01, 1, b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sequence);
    RUN_TEST(test_set_pwm_packs_and_latches_once);
    RUN_TEST(test_set_pwm_clamps);
    RUN_TEST(test_failure_propagates);
    RUN_TEST(test_restore_sequence);
    RUN_TEST(test_restore_clamps_and_stops_at_failure);
    RUN_TEST(test_failed_pwm_write_sends_no_latch);
    RUN_TEST(test_shutdown);
    return UNITY_END();
}
