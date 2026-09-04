#include "unity.h"
#include "mbp_heater.h"
#include "heater.h"
#include "mb_regmodel.h"
#include "fake_wk2132_i2c.h"
#include <string.h>

void setUp(void) {
    fake_wk2132_reset();
    heater_init(fake_wk2132_i2c(), 0U);
    mb_reg_reset();
    mbp_heater_register();
}
void tearDown(void) {}

static void test_write_request_and_level(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(53U, 1U));   /* heater_request on */
    TEST_ASSERT_EQUAL_UINT8(1U, heater_get_request());
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(54U, 7U));   /* level 7 */
    TEST_ASSERT_EQUAL_UINT8(7U, heater_get_level());
}

static void test_request_read_reflects_state(void) {
    heater_set_request(1U);
    uint16_t v = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(53U, &v));
    TEST_ASSERT_EQUAL_UINT16(1U, v);
}

static void test_level_read_reflects_state(void) {
    heater_set_level(5U);
    uint16_t v = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(54U, &v));
    TEST_ASSERT_EQUAL_UINT16(5U, v);
}

static void test_level_out_of_range_rejected(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 11U));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(54U, 0U));
}

static void test_request_out_of_range_rejected(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(53U, 2U));
}

static void test_telemetry_regs_are_readonly(void) {
    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(55U, &v));            /* state readable */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(55U, 1U)); /* RO */
}

static void test_reg67_addressable_reg71_out_of_range(void) {
    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(67U, &v));
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_read(71U, &v));
}

static void test_all_telemetry_regs_readable(void) {
    uint16_t v;
    for (uint16_t r = 55U; r <= 67U; r++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(MB_EXC_NONE, mb_reg_read(r, &v), "reg not readable");
    }
}

static void test_telemetry_reflects_snapshot(void) {
    /* Drive a running response through the fake bus so the snapshot has
       non-zero fields, then confirm the register reads mirror it. */
    uint8_t frame[56];
    heater_set_request(1U);
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xAA; frame[1] = 0x77;
    frame[5] = 3U;   /* state = running */
    frame[6] = 4U;   /* active level */
    frame[7] = 0U;   /* error */
    frame[55] = VEVOR_Checksum(frame, 55);
    fake_wk2132_inject_rx(frame, sizeof(frame));
    heater_process(0U);
    heater_process(20U);

    heater_telemetry_t t;
    heater_get_telemetry(&t);

    uint16_t v;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(55U, &v)); TEST_ASSERT_EQUAL_UINT16(t.state, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(56U, &v)); TEST_ASSERT_EQUAL_UINT16(t.active_level, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(57U, &v)); TEST_ASSERT_EQUAL_UINT16(t.error, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(58U, &v)); TEST_ASSERT_EQUAL_UINT16(t.supply_mv, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(59U, &v)); TEST_ASSERT_EQUAL_UINT16(t.fan_rpm, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(60U, &v)); TEST_ASSERT_EQUAL_UINT16(t.pump_hz_x10, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(61U, &v)); TEST_ASSERT_EQUAL_UINT16(t.exchanger_raw, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(62U, &v)); TEST_ASSERT_EQUAL_UINT16(t.state_seconds, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(63U, &v)); TEST_ASSERT_EQUAL_UINT16(t.age_ms, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(64U, &v)); TEST_ASSERT_EQUAL_UINT16(t.flags, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(65U, &v)); TEST_ASSERT_EQUAL_UINT16(t.valid_frames, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(66U, &v)); TEST_ASSERT_EQUAL_UINT16(t.checksum_failures, v);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(67U, &v)); TEST_ASSERT_EQUAL_UINT16(t.transport_errors, v);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_request_and_level);
    RUN_TEST(test_request_read_reflects_state);
    RUN_TEST(test_level_read_reflects_state);
    RUN_TEST(test_level_out_of_range_rejected);
    RUN_TEST(test_request_out_of_range_rejected);
    RUN_TEST(test_telemetry_regs_are_readonly);
    RUN_TEST(test_reg67_addressable_reg71_out_of_range);
    RUN_TEST(test_all_telemetry_regs_readable);
    RUN_TEST(test_telemetry_reflects_snapshot);
    return UNITY_END();
}
