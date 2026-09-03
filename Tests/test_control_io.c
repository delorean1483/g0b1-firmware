#include <string.h>
#include "unity.h"
#include "control.h"
#include "bsp_io.h"
#include "io_debounce.h"
#include "mb_regmodel.h"
#include "fake_bsp_io.h"

static apu_ctx_t ctx;
static bsp_io_backend_t io_be;

void setUp(void) {
    fake_bsp_io_init(&io_be); bsp_io_init(&io_be);
    mb_reg_reset();
    control_init(&ctx);
    control_inputs_init(&ctx);
    control_regs_register(&ctx);
}
void tearDown(void) {}

static void test_debounced_oil_input_reaches_ctx_and_reg7(void) {
    fake_bsp_io_set_input(IN_OIL_PRESSURE, true);
    for (unsigned int i = 0; i < CONTROL_INPUT_DEBOUNCE_TIME; i++) control_inputs_service(&ctx); /* 50 samples -> commit */
    TEST_ASSERT_TRUE(ctx.in_oil_pressure_ok);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(7, &o));   /* reg 7 = oil-pressure state */
    TEST_ASSERT_EQUAL_UINT16(1, o);
}

static void test_truck_ignition_reg8(void) {
    fake_bsp_io_set_input(IN_TRUCK_IGNITION, true);
    for (unsigned int i = 0; i < CONTROL_INPUT_DEBOUNCE_TIME; i++) control_inputs_service(&ctx);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(8, &o));
    TEST_ASSERT_EQUAL_UINT16(1, o);
}

static void test_op_mode_reg10_rw_drives_ctx(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(10, MODE_CLIMATE));
    TEST_ASSERT_EQUAL_UINT8(MODE_CLIMATE, ctx.mode_request);
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(10, &o));
    TEST_ASSERT_EQUAL_UINT16(MODE_CLIMATE, o);
}

static void test_status_regs_are_read_only(void) {
    ctx.error_state = ERR_LOW_OIL; ctx.control_status = ST_COOLING; ctx.engine_op_status = ST_RUNNING;
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(17, &o)); TEST_ASSERT_EQUAL_UINT16(ERR_LOW_OIL, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(22, &o)); TEST_ASSERT_EQUAL_UINT16(ST_RUNNING, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(23, &o)); TEST_ASSERT_EQUAL_UINT16(ST_COOLING, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(17, 0)); /* read-only */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(22, 0));
}

static void test_temp_display_reg33_rw_and_range(void) {
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(33, TD_CC_SETTING));
    TEST_ASSERT_EQUAL_UINT8(TD_CC_SETTING, ctx.temp_display_state);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(33, 5));  /* > TD_CS_SETTING */
}

static void test_mode_off_exits_diag(void) {
    ctx.op_state = OP_DIAG;
    ctx.out.compressor_clutch = true;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(10, MODE_OFF));
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
}

static void test_any_mode_write_exits_diag(void) {
    /* I3: ANY reg-10 write while in OP_DIAG must be a clean exit -- not just
     * MODE_OFF -- so diag-energized outputs never bleed into another mode. */
    ctx.op_state = OP_DIAG;
    ctx.out.compressor_clutch = true;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(10, MODE_CLIMATE));
    TEST_ASSERT_EQUAL_INT(OP_OFF, ctx.op_state);
    TEST_ASSERT_FALSE(ctx.out.compressor_clutch);
}

static void test_fw_version_reg2_is_read_only(void) {
    uint16_t o = 0;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(2, &o));
    TEST_ASSERT_EQUAL_UINT16(10100, o);   /* v1.1.0 -> 1*10000 + 1*100 + 0 */
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_ADDRESS, mb_reg_write(2, 1));   /* read-only */
}

static void test_fan_auto_reg_roundtrip_and_range(void) {
    apu_ctx_t ctx; memset(&ctx, 0, sizeof ctx);
    mb_reg_reset();
    control_regs_register(&ctx);
    uint16_t o = 0xFF;
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(9, 1));
    TEST_ASSERT_TRUE(ctx.fan_auto);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_read(9, &o));
    TEST_ASSERT_EQUAL_UINT16(1, o);
    TEST_ASSERT_EQUAL_INT(MB_EXC_NONE, mb_reg_write(9, 0));
    TEST_ASSERT_FALSE(ctx.fan_auto);
    TEST_ASSERT_EQUAL_INT(MB_EXC_ILLEGAL_VALUE, mb_reg_write(9, 2));   /* out of range */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_debounced_oil_input_reaches_ctx_and_reg7);
    RUN_TEST(test_truck_ignition_reg8);
    RUN_TEST(test_op_mode_reg10_rw_drives_ctx);
    RUN_TEST(test_status_regs_are_read_only);
    RUN_TEST(test_temp_display_reg33_rw_and_range);
    RUN_TEST(test_mode_off_exits_diag);
    RUN_TEST(test_any_mode_write_exits_diag);
    RUN_TEST(test_fw_version_reg2_is_read_only);
    RUN_TEST(test_fan_auto_reg_roundtrip_and_range);
    return UNITY_END();
}
