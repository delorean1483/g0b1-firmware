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
    TEST_ASSERT_EQUAL_HEX8(IS31_SHUTDOWN_OFF, f.regs[IS31_REG_SHUTDOWN]);
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

static unsigned count_reg(uint8_t reg);

/* Reassert the operating mode on every lit apply without adding the other
   full-restore writes. The PWM write must still precede its latch. */
static void test_apply_restores_mode_before_pwm_and_latch(void)
{
    boot();
    rgb_set_logical(1, 2, 3); rgb_process(5);
    unsigned n0 = f.nlog;
    rgb_set_logical(40, 50, 60); rgb_process(10);
    TEST_ASSERT_EQUAL_UINT(n0 + 3, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.log[n0].reg);
    TEST_ASSERT_EQUAL_HEX8(0x19, f.log[n0 + 1].reg);
    TEST_ASSERT_EQUAL_HEX8(0x28, f.log[n0 + 2].reg);
    TEST_ASSERT_EQUAL_HEX8(0xC5, f.log[n0 + 2].data[0]);
}

static void test_zero_stays_in_shutdown_after_failures_and_refresh(void)
{
    boot();
    rgb_set_logical(255, 255, 0); rgb_process(10);
    TEST_ASSERT_EQUAL_HEX8(0x35, f.regs[IS31_REG_SHUTDOWN]);
    rgb_set_logical(0, 0, 0);
    f.fail = true;
    rgb_process(20); rgb_process(520); rgb_process(1520);
    f.fail = false;
    f.nlog = 0;
    rgb_process(2520);
    TEST_ASSERT_EQUAL_UINT(1, count_reg(IS31_REG_RESET));
    for (unsigned i = 0; i < f.nlog; ++i)
        if (f.log[i].reg == IS31_REG_SHUTDOWN)
            TEST_ASSERT_EQUAL_HEX8(IS31_SHUTDOWN_OFF, f.log[i].data[0]);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
    f.nlog = 0;
    rgb_process(3520);
    TEST_ASSERT_EQUAL_UINT(2, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_SHUTDOWN, f.log[0].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_SHUTDOWN_OFF, f.log[0].data[0]);
    f.nlog = 0;
    rgb_set_logical(255, 255, 0); rgb_process(3521);
    TEST_ASSERT_EQUAL_UINT(8, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(0x05, f.log[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_PWM_UPDATE, f.log[6].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_SHUTDOWN, f.log[7].reg);
    TEST_ASSERT_EQUAL_HEX8(0x35, f.log[7].data[0]);
    f.nlog = 0;
    rgb_set_logical(0, 0, 0); rgb_process(3522);
    TEST_ASSERT_EQUAL_UINT(2, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_SHUTDOWN, f.log[0].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_SHUTDOWN_OFF, f.log[0].data[0]);
}

static void test_lit_color_change_updates_enables_after_latch(void)
{
    boot();
    rgb_set_logical(255, 255, 0); rgb_process(10);
    TEST_ASSERT_EQUAL_HEX8(0x35, f.regs[IS31_REG_SHUTDOWN]);
    f.nlog = 0;
    rgb_set_logical(0, 0, 255); rgb_process(20);
    TEST_ASSERT_EQUAL_UINT(5, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_PWM_UPDATE, f.log[2].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_SHUTDOWN, f.log[3].reg);
    TEST_ASSERT_EQUAL_HEX8(0x45, f.log[3].data[0]);
    rgb_set_logical(255, 255, 0); rgb_process(30);
    TEST_ASSERT_EQUAL_HEX8(0x35, f.regs[IS31_REG_SHUTDOWN]);
}

static void test_idle_refreshes_every_second(void)
{
    static const uint8_t want[6] = {0x00, 0x00, 0xFF, 0x0F, 0x00, 0x00}; /* red 255 -> OUT2 */
    boot();
    rgb_set_logical(255, 0, 0); rgb_process(10);   /* apply at t=10 */
    unsigned n = f.attempts;
    now = 20; run_to(1000);
    rgb_process(1009);
    TEST_ASSERT_EQUAL_UINT(n, f.attempts);          /* nothing through t+999 */
    f.nlog = 0;
    rgb_process(1010);
    TEST_ASSERT_EQUAL_UINT(n + 14, f.attempts);      /* exactly one refresh burst */
    TEST_ASSERT_EQUAL_UINT(8, f.nlog);
    TEST_ASSERT_EQUAL_UINT(0, count_reg(0x3F));     /* no reset in the burst */
    TEST_ASSERT_EQUAL_HEX8(0x19, f.log[4].reg);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, f.log[4].data, 6);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.log[6].reg);
    TEST_ASSERT_EQUAL_HEX8(0x25, f.log[6].data[0]);
    n = f.attempts;
    now = 1020; run_to(2000);
    rgb_process(2009);
    TEST_ASSERT_EQUAL_UINT(n, f.attempts);          /* quiet until the next period */
    rgb_process(2010);
    TEST_ASSERT_EQUAL_UINT(n + 14, f.attempts);
}

static void test_refresh_restores_after_chip_reset(void)
{
    boot();
    rgb_set_logical(255, 128, 1); rgb_process(10);
    memset(f.regs, 0, sizeof(f.regs));              /* chip power-on reset */
    now = 20; run_to(1000);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.regs[0x01]);     /* not yet */
    rgb_process(1010);
    TEST_ASSERT_EQUAL_HEX8(0x75, f.regs[0x01]);
    TEST_ASSERT_EQUAL_HEX8(0x08, f.regs[0x19]);     /* green 128 -> OUT1 = 2056 = 0x808 */
    TEST_ASSERT_EQUAL_HEX8(0x08, f.regs[0x1A]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.regs[0x1B]);     /* red 255 -> OUT2 */
    TEST_ASSERT_EQUAL_HEX8(0x0F, f.regs[0x1C]);
    TEST_ASSERT_EQUAL_HEX8(0x10, f.regs[0x1D]);     /* blue 1 -> OUT3 = 16 */
}

/* After a failed transfer the chip state is unknown, so the retry rewrites the
   configuration, but without the 0x3F reset: a reset blanks a lit lamp and
   re-lighting it from dark is the bench trigger for new bus faults. */
static void test_refresh_failure_restores_without_reset(void)
{
    uint8_t a[3];
    boot();
    rgb_set_logical(10, 20, 30); rgb_process(10);
    now = 20; run_to(1000);
    f.fail = true;
    rgb_process(1010);                             /* refresh fails */
    TEST_ASSERT_BITS_LOW(RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT, rgb_get_status());
    f.fail = false;
    memset(f.regs, 0, sizeof(f.regs));              /* the chip may have lost its settings */
    unsigned n = f.attempts, resets = count_reg(0x3F);
    rgb_process(1509);
    TEST_ASSERT_EQUAL_UINT(n, f.attempts);          /* backoff 500 */
    rgb_process(1510);                             /* restore, no reset */
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_EQUAL_UINT(resets, count_reg(0x3F));
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(10, a[0]); TEST_ASSERT_EQUAL_UINT8(20, a[1]); TEST_ASSERT_EQUAL_UINT8(30, a[2]);
    TEST_ASSERT_EQUAL_HEX8(0x75, f.regs[0x01]);
    TEST_ASSERT_EQUAL_HEX8(0x41, f.regs[0x19]);     /* green 20 -> 321 = 0x141 */
}

static void test_absent_chip_backs_off(void)
{
    static const uint32_t t[] = {500, 1500, 2500, 3500, 4500, 5500};
    f.fail = true;
    rgb_process(0);
    TEST_ASSERT_BITS_LOW(RGB_ST_PRESENT, rgb_get_status());
    TEST_ASSERT_EQUAL_UINT(4, f.attempts);
    rgb_process(499);
    TEST_ASSERT_EQUAL_UINT(4, f.attempts);
    for (unsigned i = 0; i < 6; i++) {
        rgb_process(t[i] - 1);
        TEST_ASSERT_EQUAL_UINT(4 * (i + 1), f.attempts);
        rgb_process(t[i]);
        TEST_ASSERT_EQUAL_UINT(4 * (i + 2), f.attempts);
    }
}

static unsigned count_reg(uint8_t reg)
{
    unsigned n = 0;
    for (unsigned i = 0; i < f.nlog; i++) if (f.log[i].reg == reg) n++;
    return n;
}

static void test_apply_failure_restores_without_reset(void)
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
    memset(f.regs, 0, sizeof(f.regs));      /* the chip may have lost its settings */
    unsigned a0 = f.attempts, resets = count_reg(0x3F);
    rgb_process(519);                       /* retry is at 20 + 500 */
    TEST_ASSERT_EQUAL_UINT(a0, f.attempts);
    rgb_process(520);                       /* one call: restore configuration + color */
    TEST_ASSERT_EQUAL_UINT(resets, count_reg(0x3F));
    TEST_ASSERT_EQUAL_HEX8(0x75, f.regs[0x01]);     /* normal operation */
    TEST_ASSERT_EQUAL_HEX8(0x3F, f.regs[0x03]);     /* current */
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.regs[0x10]);     /* scaling OUT1..3 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.regs[0x12]);
    TEST_ASSERT_EQUAL_HEX8(0xC5, f.regs[0x27]);     /* current/scaling latched */
    TEST_ASSERT_EQUAL_HEX8(0x23, f.regs[0x19]);     /* green 50 -> OUT1 = 803 = 0x323 */
    TEST_ASSERT_EQUAL_HEX8(0x03, f.regs[0x1A]);
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(40, a[0]); TEST_ASSERT_EQUAL_UINT8(50, a[1]); TEST_ASSERT_EQUAL_UINT8(60, a[2]);
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_LOW(RGB_ST_PENDING, rgb_get_status());
}

/* A chip that keeps failing may really have reset or vanished: after three
   consecutive failures the next attempt re-initializes it with the reset. */
static void test_three_failures_reinit_with_reset(void)
{
    uint8_t a[3];
    boot();
    rgb_set_logical(10, 20, 30); rgb_process(10);
    unsigned resets = count_reg(0x3F);
    f.fail_pwm = true;                      /* config writes succeed, PWM fails */
    rgb_set_logical(40, 50, 60); rgb_process(20);   /* failure 1, retry at 520 */
    rgb_process(520);                       /* failure 2: restore attempt, retry at 1520 */
    rgb_process(1520);                      /* failure 3: re-init due at 2520 */
    TEST_ASSERT_EQUAL_UINT(resets, count_reg(0x3F));   /* no reset while retrying */
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT, rgb_get_status());
    f.fail_pwm = false;
    rgb_process(2519);
    TEST_ASSERT_EQUAL_UINT(resets, count_reg(0x3F));
    rgb_process(2520);                      /* re-init with reset, then apply */
    TEST_ASSERT_EQUAL_UINT(resets + 1, count_reg(0x3F));
    rgb_get_applied(a);
    TEST_ASSERT_EQUAL_UINT8(40, a[0]); TEST_ASSERT_EQUAL_UINT8(50, a[1]); TEST_ASSERT_EQUAL_UINT8(60, a[2]);
    TEST_ASSERT_BITS_HIGH(RGB_ST_PRESENT | RGB_ST_APPLY_OK, rgb_get_status());
}

static void test_apply_failure_backs_off(void)
{
    static const uint32_t want[] = {510, 1510, 2510, 3510};   /* gaps 500, 1000, 1000 */
    unsigned seen = 0;
    boot();
    f.fail_pwm = true;                      /* init keeps succeeding, PWM write keeps failing */
    rgb_set_logical(1, 2, 3);
    for (uint32_t t = 10; t <= 3510; t += 10) {
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
    TEST_ASSERT_EQUAL_UINT(4, f.attempts);
    rgb_process(0xFFFFFFFFu);               /* a plain >= would retry here */
    rgb_process(0x000000F3u);               /* deadline is 0x000000F4 */
    TEST_ASSERT_EQUAL_UINT(4, f.attempts);
    rgb_process(0x000000F4u);
    TEST_ASSERT_EQUAL_UINT(8, f.attempts);
}

static void test_absent_chip_recovers(void)
{
    uint8_t a[3];
    f.fail = true;
    rgb_set_logical(1, 2, 3);
    rgb_process(0); rgb_process(500);       /* next retry at 1500, backoff 1000 */
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
    TEST_ASSERT_EQUAL_UINT(a0 + 4, f.attempts);
}



static void test_mode_and_control_readback_mismatches_require_retry(void)
{
    uint8_t applied[3];
    boot();
    unsigned checks = rgb_get_diagnostic(90);
    f.corrupt_read_mode = f.corrupt_read_control = true;
    rgb_set_logical(255, 255, 0); rgb_process(10);
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(84));
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(85));
    TEST_ASSERT_EQUAL_HEX16(0x3575, rgb_get_diagnostic(91));
    TEST_ASSERT_EQUAL_UINT(checks + 1, rgb_get_diagnostic(90));
    TEST_ASSERT_BITS_LOW(RGB_ST_APPLY_OK, rgb_get_status());
    rgb_get_applied(applied);
    TEST_ASSERT_EQUAL_UINT8(0, applied[0]);
    f.corrupt_read_mode = f.corrupt_read_control = false;
    rgb_process(510);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(87));
    TEST_ASSERT_EQUAL_HEX16(0x35, rgb_get_diagnostic(88));
    TEST_ASSERT_EQUAL_HEX16(0x3575, rgb_get_diagnostic(91));
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(84));
}

static void test_failed_readback_is_counted_separately(void)
{
    boot();
    rgb_set_logical(128, 0, 0); rgb_process(5);
    f.fail_read = true;
    rgb_set_logical(255, 0, 0); rgb_process(10);
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(86));
    TEST_ASSERT_EQUAL_UINT(0, rgb_get_diagnostic(84));
    TEST_ASSERT_EQUAL_UINT(0, rgb_get_diagnostic(85));
    TEST_ASSERT_BITS_LOW(RGB_ST_APPLY_OK, rgb_get_status());
    f.fail_read = false;
    unsigned writes = f.nlog;
    rgb_process(510);
    TEST_ASSERT_EQUAL_UINT(writes, f.nlog);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
}


static void test_fresh_off_command_bypasses_existing_backoff(void)
{
    boot();
    rgb_set_logical(20, 30, 40); rgb_process(10);
    f.fail = true;
    rgb_set_logical(50, 60, 70); rgb_process(20);
    unsigned attempts = f.attempts;
    rgb_process(30);
    TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
    f.fail = false;
    rgb_set_logical(0, 0, 0); rgb_process(40);
    TEST_ASSERT_EQUAL_UINT(attempts + 4, f.attempts);
    TEST_ASSERT_EQUAL_HEX8(IS31_SHUTDOWN_OFF, f.regs[IS31_REG_SHUTDOWN]);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_BITS_LOW(RGB_ST_PENDING, rgb_get_status());
}

static void test_corrupted_failed_write_restores_and_verifies_in_same_pass(void)
{
    boot();
    rgb_set_logical(255, 255, 0);
    rgb_process(10);
    f.nlog = 0;
    f.corrupt_next_write = true;
    rgb_set_logical(161, 161, 0);
    rgb_process(20);
    TEST_ASSERT_EQUAL_UINT(8, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_CONFIG, f.log[0].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_PWM_UPDATE, f.log[5].reg);
    TEST_ASSERT_EQUAL_HEX8(IS31_REG_SHUTDOWN, f.log[6].reg);
    TEST_ASSERT_EQUAL_HEX8(0x35, rgb_get_diagnostic(88));
    TEST_ASSERT_EQUAL_HEX8(0, rgb_get_diagnostic(87));
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(139));
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(140));
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
}

static void test_immediate_restore_with_bad_readback_uses_backoff(void)
{
    boot();
    f.corrupt_next_write = true;
    f.corrupt_read_mode = true;
    rgb_set_logical(161, 161, 0);
    rgb_process(10);
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(139));
    TEST_ASSERT_EQUAL_UINT(0, rgb_get_diagnostic(140));
    TEST_ASSERT_BITS_LOW(RGB_ST_APPLY_OK, rgb_get_status());
    unsigned attempts = f.attempts;
    rgb_process(509);
    TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
}

static void test_wake_clears_copied_mode_before_enabling_pwm(void)
{
    boot();
    f.mode_on_enable = 0x35;
    rgb_set_logical(0, 128, 93);
    rgb_process(20);
    TEST_ASSERT_EQUAL_UINT(1, rgb_get_diagnostic(141));
    TEST_ASSERT_EQUAL_HEX16(0x55, rgb_get_diagnostic(1507));
    TEST_ASSERT_EQUAL_HEX16(0x70, rgb_get_diagnostic(1508));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1509));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1510));
    TEST_ASSERT_EQUAL_HEX16(5, rgb_get_diagnostic(1512));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1513));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1514));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1515));
    TEST_ASSERT_EQUAL_HEX8(0x05, f.log[10].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, f.log[17].data[0]);
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(1511));
    TEST_ASSERT_EQUAL_HEX16(0, rgb_get_diagnostic(87));
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
}

static uint32_t retry_tick;
static uint16_t retry_lines;
static uint16_t retry_snapshot(uint32_t *tick)
{
    *tick = retry_tick;
    return retry_lines;
}

static void test_idle_bus_fast_retry(void)
{
    boot();
    rgb_set_bus_snapshot(retry_snapshot);
    rgb_set_logical(0, 128, 93); rgb_process(10);
    retry_tick = 25; retry_lines = 3;
    f.fail = true;
    rgb_set_logical(0, 81, 58); rgb_process(20);
    TEST_ASSERT_EQUAL_UINT16(1, rgb_get_diagnostic(153));
    TEST_ASSERT_EQUAL_UINT16(25, rgb_get_diagnostic(1889));
    TEST_ASSERT_EQUAL_HEX16(0x301, rgb_get_diagnostic(1894));
    unsigned attempts = f.attempts;
    f.fail = false;
    rgb_process(74);
    TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
    unsigned writes = f.nlog;
    f.fail_next_read = true;
    retry_tick = 75;
    rgb_process(75);
    TEST_ASSERT_EQUAL_UINT(attempts + 5, f.attempts);
    TEST_ASSERT_EQUAL_UINT(writes + 2, f.nlog);
    TEST_ASSERT_EQUAL_HEX8(0x19, f.log[writes].reg);
    TEST_ASSERT_EQUAL_HEX8(0x28, f.log[writes + 1].reg);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
    TEST_ASSERT_EQUAL_HEX16(0x307, rgb_get_diagnostic(1902));
    TEST_ASSERT_EQUAL_HEX16(0x302, rgb_get_diagnostic(1910));
}

static void test_fast_retry_restores_confirmed_mode_mismatch(void)
{
    boot();
    rgb_set_bus_snapshot(retry_snapshot);
    rgb_set_logical(0, 128, 93); rgb_process(10);
    retry_lines = 3; retry_tick = 20; f.fail = true;
    rgb_set_logical(0, 81, 58); rgb_process(20);
    f.fail = false; f.regs[2] = 0x35;
    unsigned writes = f.nlog;
    retry_tick = 70; rgb_process(70);
    TEST_ASSERT_EQUAL_HEX8(0x19, f.log[writes].reg);
    TEST_ASSERT_EQUAL_HEX8(0x28, f.log[writes + 1].reg);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.log[writes + 2].reg);
    TEST_ASSERT_EQUAL_UINT16(1, rgb_get_diagnostic(140));
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
}

static void test_fast_retry_rechecks_pins(void)
{
    boot();
    rgb_set_bus_snapshot(retry_snapshot);
    rgb_set_logical(0, 128, 93); rgb_process(10);
    retry_tick = 20; retry_lines = 3;
    f.fail = true;
    rgb_set_logical(0, 81, 58); rgb_process(20);
    unsigned attempts = f.attempts;
    retry_tick = 70; retry_lines = 1;
    rgb_process(70);
    TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
    TEST_ASSERT_EQUAL_HEX16(0x104, rgb_get_diagnostic(1902));
    retry_tick = 569; retry_lines = 3; f.fail = false;
    rgb_process(569);
    TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
    retry_tick = 570;
    rgb_process(570);
    TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
}

#ifdef RGB_BENCH_INJECTION
static void test_injected_dim_paths_leave_pwm_unchanged_until_retry(void)
{
    for (uint16_t mode = 1; mode <= 2; ++mode) {
        setUp();
        boot();
        rgb_set_bus_snapshot(retry_snapshot);
        retry_lines = 3;
        rgb_set_logical(0, 128, 93); rgb_process(10);
        TEST_ASSERT_EQUAL_INT(0, rgb_bench_arm(mode));
        unsigned attempts = f.attempts;
        unsigned latches = f.pwm_latches;
        retry_tick = 20;
        rgb_set_logical(0, 81, 58); rgb_process(20);
        TEST_ASSERT_EQUAL_UINT16(0, rgb_get_diagnostic(154));
        TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
        TEST_ASSERT_EQUAL_UINT(latches, f.pwm_latches);
        TEST_ASSERT_EQUAL_HEX16(0x0301, rgb_get_diagnostic(1894));
        retry_tick = 69; rgb_process(69);
        TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
        retry_tick = 70; rgb_process(70);
        if (mode == 2u) {
            TEST_ASSERT_EQUAL_HEX16(0x0104, rgb_get_diagnostic(1902));
            TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
            retry_tick = 569; rgb_process(569);
            TEST_ASSERT_EQUAL_UINT(attempts, f.attempts);
            retry_tick = 570; rgb_process(570);
            TEST_ASSERT_EQUAL_HEX16(0x0306, rgb_get_diagnostic(1910));
        } else {
            TEST_ASSERT_EQUAL_HEX16(0x0307, rgb_get_diagnostic(1902));
            TEST_ASSERT_EQUAL_HEX16(0x0302, rgb_get_diagnostic(1910));
        }
        TEST_ASSERT_EQUAL_UINT(latches + 1u, f.pwm_latches);
        TEST_ASSERT_BITS_HIGH(RGB_ST_APPLY_OK, rgb_get_status());
    }
}
#endif

int main(void)
{
    UNITY_BEGIN();
#ifdef RGB_BENCH_INJECTION
    RUN_TEST(test_injected_dim_paths_leave_pwm_unchanged_until_retry);
#endif
    RUN_TEST(test_idle_bus_fast_retry);
    RUN_TEST(test_fast_retry_restores_confirmed_mode_mismatch);
    RUN_TEST(test_fast_retry_rechecks_pins);
    RUN_TEST(test_wake_clears_copied_mode_before_enabling_pwm);
    RUN_TEST(test_corrupted_failed_write_restores_and_verifies_in_same_pass);
    RUN_TEST(test_immediate_restore_with_bad_readback_uses_backoff);
    RUN_TEST(test_fresh_off_command_bypasses_existing_backoff);
    RUN_TEST(test_mode_and_control_readback_mismatches_require_retry);
    RUN_TEST(test_failed_readback_is_counted_separately);
    RUN_TEST(test_boot_inits_and_is_off);
    RUN_TEST(test_red_maps_to_out2);
    RUN_TEST(test_green_out1_blue_out3);
    RUN_TEST(test_to12_values);
    RUN_TEST(test_last_request_wins);
    RUN_TEST(test_apply_restores_mode_before_pwm_and_latch);
    RUN_TEST(test_zero_stays_in_shutdown_after_failures_and_refresh);
    RUN_TEST(test_lit_color_change_updates_enables_after_latch);
    RUN_TEST(test_idle_refreshes_every_second);
    RUN_TEST(test_refresh_restores_after_chip_reset);
    RUN_TEST(test_refresh_failure_restores_without_reset);
    RUN_TEST(test_absent_chip_backs_off);
    RUN_TEST(test_apply_failure_restores_without_reset);
    RUN_TEST(test_three_failures_reinit_with_reset);
    RUN_TEST(test_apply_failure_backs_off);
    RUN_TEST(test_backoff_wraps_past_zero);
    RUN_TEST(test_absent_chip_recovers);
    return UNITY_END();
}

