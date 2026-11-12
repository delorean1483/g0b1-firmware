#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "fake_coproc_bus.h"
#include "unity.h"
#include <string.h>

static fake_coproc_t c;
static uint32_t now;
static void run_to(uint32_t t){ while((int32_t)(t - now) > 0){ heater_process(now); now += 10U; } heater_process(now); }
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }
static void put16(uint8_t r, uint16_t v){ c.regs[r]=(uint8_t)v; c.regs[r+1]=(uint8_t)(v>>8); }

void setUp(void){ fake_coproc_init(&c); now = 0; heater_init(&c.api, NULL, now); }
void tearDown(void){}

void test_identifies_then_writes_control_block_every_250ms(void){
    heater_set_request(1); heater_set_level(6); heater_set_setpoint_c(22); heater_set_ambient_f(77);
    run_to(1000);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_EQUAL_HEX8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    TEST_ASSERT_EQUAL_UINT8(6, c.regs[HTR_R_LEVEL]);
    TEST_ASSERT_EQUAL_INT8(22, (int8_t)c.regs[HTR_R_SETPOINT_C]);
    TEST_ASSERT_EQUAL_HEX16(250, (uint16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8))); /* 77F = 25.0C */
    TEST_ASSERT_EQUAL_UINT8(HEATER_HOST_TIMEOUT_S, c.regs[HTR_R_HOST_TIMEOUT_S]);
    TEST_ASSERT_EQUAL_HEX8(HTR_R_RUN_REQUEST, c.last_write_reg);  /* one block, 0x11..0x17 */
    TEST_ASSERT_EQUAL_UINT16(7, c.last_write_len);
    uint8_t hb = c.regs[HTR_R_HOST_HEARTBEAT];
    unsigned w = c.writes; run_to(2000);
    TEST_ASSERT_TRUE(c.writes - w >= 3 && c.writes - w <= 5);
    TEST_ASSERT_NOT_EQUAL(hb, c.regs[HTR_R_HOST_HEARTBEAT]);     /* changes every poll */
}
void test_wrong_device_is_absent_and_never_written(void){
    c.regs[HTR_R_DEVICE_ID] = 0x12;
    heater_set_request(1); run_to(5000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_COMMS_FAULT);
}
void test_unknown_map_version_is_not_controlled(void){
    c.regs[HTR_R_MAP_VERSION] = 1;                /* v1 coprocessor: unkeyed start */
    run_to(5000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
}
void test_status_decodes_into_legacy_and_new_fields(void){
    c.regs[HTR_R_PHASE] = HTR_PHASE_COOLDOWN; c.regs[HTR_R_ACTIVE_TYPE] = HTR_TYPE_AUTOTERM;
    put16(HTR_R_FLAGS, HTR_F_FRESH | HTR_F_PRESENT); put16(HTR_R_SUPPLY_MV, 12600);
    c.regs[HTR_R_VENDOR_STATE] = 4; c.regs[HTR_R_VENDOR_SUB] = 0;
    put16(HTR_R_CAPS, HTR_CAP_SETPOINT); c.regs[HTR_R_LINE_SENSE] = 0x22; c.regs[HTR_R_DETECT_RESULT] = HTR_DET_FOUND;
    run_to(1500);
    heater_telemetry_t t = tel();
    TEST_ASSERT_EQUAL_UINT8(4, t.state);                      /* legacy cooldown */
    TEST_ASSERT_EQUAL_UINT8(HTR_PHASE_COOLDOWN, t.phase);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_COOLDOWN);
    TEST_ASSERT_TRUE(t.flags & HEATER_FLAG_FRESH);
    TEST_ASSERT_EQUAL_UINT16(12600, t.supply_mv);
    TEST_ASSERT_EQUAL_HEX16(0x0400, t.vendor_state);
    TEST_ASSERT_EQUAL_HEX16(0x0222, t.detect);
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_AUTOTERM, t.type);
}
void test_legacy_state_collapse(void){
    const uint8_t phase[7] = {0,1,2,3,4,5,6};
    const uint8_t legacy[7] = {0,0,1,3,4,4,0};
    for (int i = 0; i < 7; i++) {
        c.regs[HTR_R_PHASE] = phase[i]; run_to(now + 300);
        TEST_ASSERT_EQUAL_UINT8(legacy[i], tel().state);
    }
}
void test_fault_without_code_reports_ff(void){
    c.regs[HTR_R_PHASE] = HTR_PHASE_FAULT; c.regs[HTR_R_FAULT_CODE] = 0; run_to(1500);
    TEST_ASSERT_EQUAL_HEX8(0xFF, tel().error);
    c.regs[HTR_R_FAULT_CODE] = 7; run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(7, tel().error);
}
void test_i2c_failure_sets_sticky_xport_and_reidentifies(void){
    unsigned resets_before;
    run_to(1500); TEST_ASSERT_TRUE(tel().present);
    c.fail = true; run_to(3000);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_XPORT_FAULT);
    TEST_ASSERT_TRUE(tel().transport_errors > 0);
    run_to(8000);
    TEST_ASSERT_FALSE(tel().present);                         /* lost after 5 s */
    c.fail = false; resets_before = c.reads; run_to(10000);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_TRUE(c.reads > resets_before);
    run_to(14000);
    TEST_ASSERT_FALSE(tel().flags & HEATER_FLAG_XPORT_FAULT);  /* sticky window expired */
}
static unsigned s_resets; static void reset_hook(void){ s_resets++; }
void test_reset_hook_called_when_lost(void){
    s_resets = 0; heater_init(&c.api, reset_hook, now);
    run_to(1500); c.fail = true; run_to(8000);
    TEST_ASSERT_EQUAL(1u, s_resets);
}
void test_type_select_forwarded_once_and_read_back(void){
    run_to(1500);
    TEST_ASSERT_EQUAL_HEX8(0xFF, heater_get_type_select());
    unsigned w = c.writes; heater_set_type_select(2); run_to(1800);
    TEST_ASSERT_EQUAL_UINT8(2, c.regs[HTR_R_HEATER_TYPE]);
    run_to(3000);
    TEST_ASSERT_EQUAL_UINT8(2, heater_get_type_select());
    TEST_ASSERT_TRUE(c.writes - w <= 7);                      /* ctl each cycle + exactly one type write */
}
void test_setters_reject_out_of_range(void){
    heater_set_level(0); heater_set_level(11); TEST_ASSERT_EQUAL_UINT8(1, heater_get_level());
    heater_set_setpoint_c(4); heater_set_setpoint_c(31); TEST_ASSERT_EQUAL_INT8(20, heater_get_setpoint_c());
}
void test_ambient_conversion_rounds(void){
    heater_set_ambient_f(32); run_to(1500);
    TEST_ASSERT_EQUAL_HEX16(0, (uint16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8)));
    heater_set_ambient_f(-4); run_to(2000);                    /* -20.0 C */
    TEST_ASSERT_EQUAL_INT16(-200, (int16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8)));
}
/* Heaterless unit: nothing ACKs 0x64. Boot must not fault, nothing is
   written, and the service keeps retrying identity once a second. */
void test_no_coprocessor_at_boot(void){
    c.fail = true; run_to(10000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
    TEST_ASSERT_TRUE(c.reads >= 9 && c.reads <= 11);
    TEST_ASSERT_EQUAL_UINT8(0, tel().state);
    /* Review finding 4: absent is not a fault. */
    TEST_ASSERT_FALSE(tel().flags & HEATER_FLAG_XPORT_FAULT);
    TEST_ASSERT_EQUAL_UINT16(0, tel().transport_errors);
}
/* Review finding 3: a coprocessor FAULT latches the G0B1's request off, so
   a later coprocessor reset or CLEAR_FAULT can never re-ignite on a stale ON. */
void test_fault_latches_request_off(void){
    heater_set_request(1); run_to(1500);
    TEST_ASSERT_EQUAL_HEX8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    c.regs[HTR_R_PHASE] = HTR_PHASE_FAULT; run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    run_to(2500);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
}
/* Map v2: a host-timeout stop or a refused start also latches the request
   off, so the G0B1 never keeps replaying a start the coprocessor dropped. */
void test_host_timeout_stop_latches_request_off(void){
    heater_set_request(1); run_to(1500);
    put16(HTR_R_FLAGS, HTR_F_PRESENT | HTR_F_HOST_TIMEOUT_STOP); run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    run_to(2500);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
}
void test_refused_start_latches_request_off(void){
    heater_set_request(1); run_to(1500);
    put16(HTR_R_FLAGS, HTR_F_PRESENT | HTR_F_START_REFUSED); run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
}
/* START/STOP over Modbus reg 75 set the request (the only start path). */
void test_start_and_stop_commands_drive_the_request(void){
    run_to(1500);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_START));
    TEST_ASSERT_EQUAL_UINT8(1, heater_get_request());
    run_to(1800);
    TEST_ASSERT_EQUAL_HEX8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_STOP));
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    run_to(2100);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
    TEST_ASSERT_EQUAL(0u, c.command_writes);
}
/* Finding 3: commands (clear-fault etc.) reach the coprocessor once. */
void test_command_forwarded_once_and_result_read_back(void){
    run_to(1500);
    c.regs[HTR_R_CMD_RESULT] = HTR_RES_BUSY;
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    unsigned w0 = c.writes; run_to(1800);
    TEST_ASSERT_EQUAL_UINT8(HTR_CMD_CLEAR_FAULT, c.regs[HTR_R_COMMAND]);
    c.regs[HTR_R_COMMAND] = 0; run_to(3000);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_COMMAND]);       /* not repeated */
    TEST_ASSERT_TRUE(c.writes - w0 <= 7);
    TEST_ASSERT_EQUAL_UINT8(HTR_RES_BUSY, tel().cmd_result);
    TEST_ASSERT_FALSE(heater_send_command(0));
    TEST_ASSERT_FALSE(heater_send_command(6));
}

void test_ambiguous_command_delivery_is_not_replayed_on_later_poll(void) {
    run_to(1500);
    c.fail_command_after_accept=true;
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    run_to(1800);
    TEST_ASSERT_EQUAL_UINT(1,c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(HTR_CMD_CLEAR_FAULT,c.regs[HTR_R_COMMAND]);
    c.regs[HTR_R_COMMAND]=0;
    c.fail_command_after_accept=false;
    run_to(5000);
    TEST_ASSERT_EQUAL_UINT(1,c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(0,c.regs[HTR_R_COMMAND]);
    TEST_ASSERT_TRUE(tel().transport_errors>0);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_host_timeout_stop_latches_request_off);
    RUN_TEST(test_refused_start_latches_request_off);
    RUN_TEST(test_start_and_stop_commands_drive_the_request);
    RUN_TEST(test_ambiguous_command_delivery_is_not_replayed_on_later_poll);
    RUN_TEST(test_fault_latches_request_off);
    RUN_TEST(test_command_forwarded_once_and_result_read_back);

    RUN_TEST(test_no_coprocessor_at_boot);
    RUN_TEST(test_identifies_then_writes_control_block_every_250ms);
    RUN_TEST(test_wrong_device_is_absent_and_never_written);
    RUN_TEST(test_unknown_map_version_is_not_controlled);
    RUN_TEST(test_status_decodes_into_legacy_and_new_fields);
    RUN_TEST(test_legacy_state_collapse);
    RUN_TEST(test_fault_without_code_reports_ff);
    RUN_TEST(test_i2c_failure_sets_sticky_xport_and_reidentifies);
    RUN_TEST(test_reset_hook_called_when_lost);
    RUN_TEST(test_type_select_forwarded_once_and_read_back);
    RUN_TEST(test_setters_reject_out_of_range);
    RUN_TEST(test_ambient_conversion_rounds);
    return UNITY_END(); }
