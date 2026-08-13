#include "unity.h"
#include "mbp_rtc.h"
#include "mb_regmodel.h"
#include "rtc.h"
#include "rtc_calendar.h"
#include "nvm.h"
#include "fake_i2c.h"
#include "fake_nor.h"

static i2c_backend_t i2c;
static nvm_backend_t nor;

void setUp(void) {
    mb_reg_reset();
    fake_i2c_init(&i2c); rtc_init(&i2c);
    fake_nor_init(&nor); nvm_init(&nor);     /* calendar accessors need NVM */
    mbp_rtc_register();
}
void tearDown(void) {}

static void test_calendar_reg_roundtrip(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(24, 1));      /* state */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(27, 6));      /* month */
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(24, &o)); TEST_ASSERT_EQUAL_UINT16(1, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(27, &o)); TEST_ASSERT_EQUAL_UINT16(6, o);
}

static void test_rtcc_write_seeds_and_commits(void) {
    /* Establish a baseline clock. */
    mb_reg_write(42, 25); mb_reg_write(43, 6); mb_reg_write(44, 15);
    mb_reg_write(45, 1);  mb_reg_write(46, 13); mb_reg_write(47, 30); mb_reg_write(48, 45);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(42, &o)); TEST_ASSERT_EQUAL_UINT16(25, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(46, &o)); TEST_ASSERT_EQUAL_UINT16(13, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(48, &o)); TEST_ASSERT_EQUAL_UINT16(45, o);
}

static void test_rtcc_single_field_write_preserves_siblings(void) {
    /* Baseline, then change only the hour (reg 46); siblings must survive (stage seeded from live). */
    mb_reg_write(42, 25); mb_reg_write(43, 6); mb_reg_write(44, 15);
    mb_reg_write(45, 1);  mb_reg_write(46, 13); mb_reg_write(47, 30); mb_reg_write(48, 45);
    mb_reg_write(46, 9);                          /* change hour only */
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(46, &o)); TEST_ASSERT_EQUAL_UINT16(9, o);  /* changed */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(43, &o)); TEST_ASSERT_EQUAL_UINT16(6, o);  /* sibling intact */
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(48, &o)); TEST_ASSERT_EQUAL_UINT16(45, o); /* sibling intact */
}

static void test_reg52_read_only(void) {
    uint8_t v = 0x5A; rtc_sram_write(0, &v, 1);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(52, &o)); TEST_ASSERT_EQUAL_UINT16(0x5A, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(52, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_calendar_reg_roundtrip);
    RUN_TEST(test_rtcc_write_seeds_and_commits);
    RUN_TEST(test_rtcc_single_field_write_preserves_siblings);
    RUN_TEST(test_reg52_read_only);
    return UNITY_END();
}
