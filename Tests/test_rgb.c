#include "unity.h"
#include "rgb.h"
#include "is31fl3293.h"
#include "fake_is31.h"
#include <string.h>

static fake_is31_t f;
static uint32_t now;

static void run_to(uint32_t t)
{
    while ((int32_t)(t - now) > 0) { rgb_process(now); now += 10U; }
    rgb_process(now);
}

/* data of the most recent 0x19 write, or NULL */
static const uint8_t *last_pwm(void)
{
    for (unsigned i = f.nlog; i > 0; i--)
        if (f.log[i - 1].reg == IS31_REG_PWM) return f.log[i - 1].data;
    return NULL;
}

void setUp(void) { fake_is31_init(&f); now = 0; rgb_init(&f.api, 0); }
void tearDown(void) {}

static void boot(void) { rgb_process(0); }

static void test_boot_inits_and_is_off(void)
{
    uint8_t a[3] = {9, 9, 9};
    boot();
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(0, a[0]); TEST_ASSERT_EQUAL_UINT8(0, a[1]); TEST_ASSERT_EQUAL_UINT8(0, a[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, f.log[0].reg);
}

static void test_red_maps_to_out2(void)
{
    static const uint8_t want[6] = {0x00, 0x00, 0xFF, 0x0F, 0x00, 0x00};
    uint8_t a[3];
    boot();
    rgb_set_logical(255, 0, 0); rgb_process(10);
    TEST_ASSERT_NOT_NULL(last_pwm());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, last_pwm(), 6);
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(255, a[0]); TEST_ASSERT_EQUAL_UINT8(0, a[1]); TEST_ASSERT_EQUAL_UINT8(0, a[2]);
    TEST_ASSERT_BITS_LOW(RGB_ST_PENDING, rgb_get_status());
}

static void test_green_out1_blue_out3(void)
{
    boot();
    rgb_set_logical(0, 255, 0); rgb_process(10);
    TEST_ASSERT_EQUAL_HEX8(0xFF, last_pwm()[0]); TEST_ASSERT_EQUAL_HEX8(0x0F, last_pwm()[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, last_pwm()[2]); TEST_ASSERT_EQUAL_HEX8(0x00, last_pwm()[4]);
    rgb_set_logical(0, 0, 255); rgb_process(20);
    TEST_ASSERT_EQUAL_HEX8(0xFF, last_pwm()[4]); TEST_ASSERT_EQUAL_HEX8(0x0F, last_pwm()[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, last_pwm()[0]); TEST_ASSERT_EQUAL_HEX8(0x00, last_pwm()[2]);
}

static void test_to12_values(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, rgb_to12(0));
    TEST_ASSERT_EQUAL_UINT16(4095, rgb_to12(255));
    TEST_ASSERT_EQUAL_UINT16(2056, rgb_to12(128));
    TEST_ASSERT_EQUAL_UINT16(16, rgb_to12(1));
}

static void test_last_request_wins(void)
{
    uint8_t a[3], r[3];
    boot();
    unsigned l = f.pwm_latches;
    rgb_set_logical(1, 2, 3); rgb_set_logical(4, 5, 6);
    rgb_get_requested(r);
    TEST_ASSERT_EQUAL_UINT8(4, r[0]); TEST_ASSERT_EQUAL_UINT8(6, r[2]);
    rgb_process(10);
    TEST_ASSERT_EQUAL_UINT(l + 1, f.pwm_latches);
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(4, a[0]); TEST_ASSERT_EQUAL_UINT8(5, a[1]); TEST_ASSERT_EQUAL_UINT8(6, a[2]);
}

static void test_idle_does_no_i2c(void)
{
    boot();
    rgb_set_logical(10, 20, 30); run_to(10);
    unsigned n = f.attempts;
    now = 20; run_to(5000);
    TEST_ASSERT_EQUAL_UINT(n, f.attempts);
}

static void test_absent_chip_backs_off(void)
{
    static const uint32_t t[] = {500, 1500, 3500, 7500, 15500, 23500};
    f.fail = true;
    rgb_process(0);
    TEST_ASSERT_BITS_LOW(RGB_ST_PRESENT, rgb_get_status());
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    rgb_process(499);
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    for (unsigned i = 0; i < 6; i++) {
        rgb_process(t[i] - 1);
        TEST_ASSERT_EQUAL_UINT(i + 1, f.attempts);
        rgb_process(t[i]);
        TEST_ASSERT_EQUAL_UINT(i + 2, f.attempts);
    }
}

static unsigned count_reg(uint8_t reg)
{
    unsigned n = 0;
    for (unsigned i = 0; i < f.nlog; i++) if (f.log[i].reg == reg) n++;
    return n;
}

static void test_apply_failure_reinit_reapplies(void)
{
    uint8_t a[3];
    boot();
    rgb_set_logical(10, 20, 30); rgb_process(10);
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(10, a[0]);
    f.fail = true;
    rgb_set_logical(40, 50, 60); rgb_process(20);
    TEST_ASSERT_BITS_LOW(RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT, rgb_get_status());
    f.fail = false;
    unsigned a0 = f.attempts, resets = count_reg(0x3F);
    rgb_process(519);                       /* retry is at 20 + 500 */
    TEST_ASSERT_EQUAL_UINT(a0, f.attempts);
    rgb_process(520);                       /* one call: re-init and apply */
    TEST_ASSERT_EQUAL_UINT(resets + 1, count_reg(0x3F));
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(40, a[0]); TEST_ASSERT_EQUAL_UINT8(50, a[1]); TEST_ASSERT_EQUAL_UINT8(60, a[2]);
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_LOW(RGB_ST_PENDING, rgb_get_status());
}

static void test_apply_failure_backs_off(void)
{
    static const uint32_t want[] = {510, 1510, 3510, 7510};   /* gaps 500, 1000, 2000 */
    unsigned seen = 0;
    boot();
    f.fail_pwm = true;                      /* init keeps succeeding, PWM write keeps failing */
    rgb_set_logical(1, 2, 3);
    for (uint32_t t = 10; t <= 7600; t += 10) {
        unsigned n = f.attempts;
        rgb_process(t);
        if (f.attempts != n) {
            if (t == 10) continue;          /* the first, immediate failure */
            TEST_ASSERT_TRUE(seen < 4);
            TEST_ASSERT_EQUAL_UINT32(want[seen], t);
            seen++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(4, seen);
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT, rgb_get_status());
}

static void test_backoff_wraps_past_zero(void)
{
    rgb_init(&f.api, 0xFFFFFF00u);
    f.fail = true;
    rgb_process(0xFFFFFF00u);
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    rgb_process(0xFFFFFFFFu);               /* a plain >= would retry here */
    rgb_process(0x000000F3u);               /* deadline is 0x000000F4 */
    TEST_ASSERT_EQUAL_UINT(1, f.attempts);
    rgb_process(0x000000F4u);
    TEST_ASSERT_EQUAL_UINT(2, f.attempts);
}

static void test_absent_chip_recovers(void)
{
    uint8_t a[3];
    f.fail = true;
    rgb_set_logical(1, 2, 3);
    rgb_process(0); rgb_process(500);       /* next retry at 1500, backoff 2000 */
    f.fail = false;
    unsigned a0 = f.attempts;
    rgb_process(1499);
    TEST_ASSERT_EQUAL_UINT(a0, f.attempts);
    rgb_process(1500);                      /* init and apply in this one call */
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_LOW(RGB_ST_PENDING, rgb_get_status());
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(1, a[0]); TEST_ASSERT_EQUAL_UINT8(2, a[1]); TEST_ASSERT_EQUAL_UINT8(3, a[2]);
    /* backoff is back to the minimum: a later failure retries after 500 ms */
    f.fail = true;
    rgb_set_logical(4, 5, 6); rgb_process(1600);   /* apply fails */
    a0 = f.attempts;
    rgb_process(2099);
    TEST_ASSERT_EQUAL_UINT(a0, f.attempts);
    rgb_process(2100);
    TEST_ASSERT_EQUAL_UINT(a0 + 1, f.attempts);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_inits_and_is_off);
    RUN_TEST(test_red_maps_to_out2);
    RUN_TEST(test_green_out1_blue_out3);
    RUN_TEST(test_to12_values);
    RUN_TEST(test_last_request_wins);
    RUN_TEST(test_idle_does_no_i2c);
    RUN_TEST(test_absent_chip_backs_off);
    RUN_TEST(test_apply_failure_reinit_reapplies);
    RUN_TEST(test_apply_failure_backs_off);
    RUN_TEST(test_backoff_wraps_past_zero);
    RUN_TEST(test_absent_chip_recovers);
    return UNITY_END();
}
