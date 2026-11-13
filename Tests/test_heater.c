#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "fake_coproc_bus.h"
#include "unity.h"
#include <string.h>

static fake_coproc_t c;
static uint32_t now;
static bool cortex_live;
static void process(void) {
    if (cortex_live) heater_note_cortex_traffic(now);
    heater_process(now);
}
static void run_to(uint32_t t) {
    while ((int32_t)(t - now) > 0) { process(); now += 10U; }
    process();
}
static heater_telemetry_t tel(void){ heater_telemetry_t t; heater_get_telemetry(&t); return t; }
static void put16(uint8_t r, uint16_t v){ c.regs[r]=(uint8_t)v; c.regs[r+1]=(uint8_t)(v>>8); }

void setUp(void){ fake_coproc_init(&c); now = 0; cortex_live = true; heater_init(&c.api, NULL, now); }
void tearDown(void){}

void test_identifies_then_writes_only_changed_control(void){
    run_to(250);
    heater_set_request(1); heater_set_level(6); heater_set_setpoint_c(22); heater_set_ambient_f(77);
    run_to(1000);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    TEST_ASSERT_EQUAL_UINT8(6, c.regs[HTR_R_LEVEL]);
    TEST_ASSERT_EQUAL_INT8(22, (int8_t)c.regs[HTR_R_SETPOINT_C]);
    TEST_ASSERT_EQUAL_HEX16(250, (uint16_t)(c.regs[HTR_R_AMBIENT_C_X10] | (c.regs[HTR_R_AMBIENT_C_X10+1] << 8))); /* 77F = 25.0C */
    unsigned w = c.writes; run_to(2000);
    TEST_ASSERT_EQUAL_UINT(w + 4, c.writes);
    c.regs[HTR_R_LEVEL] = 2;
    run_to(2300);
    TEST_ASSERT_EQUAL_UINT(w + 6, c.writes);
    TEST_ASSERT_EQUAL_UINT8(6, c.regs[HTR_R_LEVEL]);
    TEST_ASSERT_EQUAL_UINT8(HTR_R_LEVEL, c.last_write_reg);
}
void test_wrong_device_is_absent_and_never_written(void){
    c.regs[HTR_R_DEVICE_ID] = 0x12;
    heater_set_request(1); run_to(5000);
    TEST_ASSERT_FALSE(tel().present);
    TEST_ASSERT_EQUAL(0u, c.writes);
    TEST_ASSERT_TRUE(tel().flags & HEATER_FLAG_COMMS_FAULT);
}
void test_unknown_map_version_is_not_controlled(void){
    c.regs[HTR_R_MAP_VERSION] = HEATER_EXPECTED_MAP_VERSION == 1 ? 2 : 1;
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
    run_to(250);
    heater_set_request(1); run_to(1500);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    c.regs[HTR_R_PHASE] = HTR_PHASE_FAULT; run_to(2000);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    run_to(2500);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
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
    TEST_ASSERT_TRUE(c.writes - w0 <= 9);
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
static uint32_t epoch;
static uint32_t recovery_epoch(void) { return epoch; }

static void test_version_read_failure_drops_start(void)
{
    run_to(500);
    heater_set_request(1);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    c.fail_identity = true;
    unsigned writes = c.writes;
    run_to(750);
    TEST_ASSERT_EQUAL_UINT(writes, c.writes);
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_MAP_UNREADABLE, tel().flags);
    TEST_ASSERT_EQUAL_UINT8(1, heater_get_request());
    TEST_ASSERT_FALSE(heater_send_command(HTR_CMD_START));
    c.fail_identity = false;
    run_to(2200);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_EQUAL_UINT(0, c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
}

static void test_recovery_requires_new_identity_before_any_write(void)
{
    epoch = 0;
    heater_set_recovery_epoch(recovery_epoch);
    run_to(500);
    heater_set_request(1);
    run_to(750);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    unsigned writes = c.writes;
    unsigned reads = c.reads;
    ++epoch;
    run_to(760);
    TEST_ASSERT_TRUE(c.reads > reads);
    TEST_ASSERT_EQUAL_UINT(writes, c.writes);
    TEST_ASSERT_EQUAL_UINT8(1, heater_get_request());
    TEST_ASSERT_BITS_LOW(HEATER_FLAG_MAP_UNREADABLE | HEATER_FLAG_COMMS_FAULT, tel().flags);
    run_to(1250);
    TEST_ASSERT_EQUAL_UINT(0, c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
}

static void test_reflash_mismatch_and_matching_reset_drop_start(void)
{
    run_to(500);
    heater_set_request(1);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    heater_set_type_select(2);
    c.regs[HTR_R_MAP_VERSION] = HEATER_EXPECTED_MAP_VERSION == 1 ? 2 : 1;
    unsigned writes = c.writes;
    run_to(1000);
    TEST_ASSERT_EQUAL_UINT(writes, c.writes);
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_MAP_MISMATCH, tel().flags);
    c.regs[HTR_R_MAP_VERSION] = HEATER_EXPECTED_MAP_VERSION;
    run_to(2200);
    TEST_ASSERT_TRUE(tel().present);
    TEST_ASSERT_EQUAL_UINT(0, c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
    heater_set_request(1);
    TEST_ASSERT_TRUE(heater_send_command(HTR_CMD_CLEAR_FAULT));
    c.regs[HTR_R_PHASE] = HTR_PHASE_DETECTING;
    run_to(2600);
    TEST_ASSERT_EQUAL_UINT(0, c.command_writes);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
}

static uint32_t mock_poll_clock(void) { return now + 6U; }

static void test_silence_quiets_v1_bus_and_requires_fresh_start(void)
{
    run_to(500);
    heater_set_request(1);
    run_to(750);
    cortex_live = false;
    run_to(6500);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
    unsigned reads = c.reads;
    unsigned writes = c.writes;
    run_to(8000);
    TEST_ASSERT_EQUAL_UINT(reads, c.reads);
    TEST_ASSERT_EQUAL_UINT(writes, c.writes);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    heater_set_request(1);
    TEST_ASSERT_FALSE(heater_send_command(HTR_CMD_START));
    cortex_live = true;
    run_to(8500);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
    run_to(9000);
    heater_set_request(1);
    run_to(9250);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
}

static void test_poll_log_tracks_due_start_duration_and_valid_gap(void)
{
    heater_set_poll_clock(mock_poll_clock);
    run_to(500);
    now = 900;
    process();
    uint16_t head = heater_poll_diagnostic(HEATER_POLL_LOG_HEAD);
    uint16_t entry = HEATER_POLL_LOG_BASE + (head - 1U) * HEATER_POLL_LOG_WORDS;
    TEST_ASSERT_EQUAL_UINT16(750, heater_poll_diagnostic(entry + 1U));
    TEST_ASSERT_EQUAL_UINT16(900, heater_poll_diagnostic(entry + 3U));
    TEST_ASSERT_EQUAL_UINT16(6, heater_poll_diagnostic(entry + 5U));
    TEST_ASSERT_EQUAL_UINT16(1, heater_poll_diagnostic(entry + 6U));
    TEST_ASSERT_EQUAL_UINT16(400, heater_poll_diagnostic(HEATER_POLL_MAX_GAP_LO));
}

static void test_local_timeout_clears_run_without_reasserting(void)
{
    run_to(500);
    heater_set_request(1);
    run_to(1000);
    TEST_ASSERT_EQUAL_UINT8(HTR_RUN_KEY, c.regs[HTR_R_RUN_REQUEST]);
    c.regs[HTR_R_RUN_REQUEST] = 0;
    put16(HTR_R_FLAGS, HTR_F_HOST_TIMEOUT_STOP);
    unsigned writes = c.writes;
    run_to(1500);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    TEST_ASSERT_EQUAL_UINT(writes + 3, c.writes);
    TEST_ASSERT_EQUAL_UINT8(0, c.regs[HTR_R_RUN_REQUEST]);
}

#ifdef HTR_F_START_REFUSED
static void test_start_refusal_clears_run_without_reasserting(void)
{
    run_to(500);
    heater_set_request(1);
    run_to(1000);
    c.regs[HTR_R_RUN_REQUEST] = 0;
    put16(HTR_R_FLAGS, HTR_F_START_REFUSED | HTR_F_START_ARMED);
    unsigned writes = c.writes;
    run_to(1500);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    TEST_ASSERT_EQUAL_UINT(writes + 2, c.writes);
    TEST_ASSERT_EQUAL_UINT(0, c.command_writes);
}
#endif

static void test_unarmed_on_sets_operator_feedback(void)
{
    TEST_ASSERT_FALSE(heater_set_request(1));
    TEST_ASSERT_BITS_HIGH(HEATER_FLAG_REQUEST_AGAIN, tel().flags);
    TEST_ASSERT_EQUAL_UINT8(0, heater_get_request());
    run_to(500);
    TEST_ASSERT_TRUE(heater_set_request(1));
    TEST_ASSERT_BITS_LOW(HEATER_FLAG_REQUEST_AGAIN, tel().flags);
}

int main(void){ UNITY_BEGIN();
    RUN_TEST(test_unarmed_on_sets_operator_feedback);
#ifdef HTR_F_START_REFUSED
    RUN_TEST(test_start_refusal_clears_run_without_reasserting);
#endif
    RUN_TEST(test_local_timeout_clears_run_without_reasserting);
    RUN_TEST(test_silence_quiets_v1_bus_and_requires_fresh_start);
    RUN_TEST(test_poll_log_tracks_due_start_duration_and_valid_gap);
    RUN_TEST(test_version_read_failure_drops_start);
    RUN_TEST(test_recovery_requires_new_identity_before_any_write);
    RUN_TEST(test_reflash_mismatch_and_matching_reset_drop_start);
    RUN_TEST(test_ambiguous_command_delivery_is_not_replayed_on_later_poll);
    RUN_TEST(test_fault_latches_request_off);
    RUN_TEST(test_command_forwarded_once_and_result_read_back);

    RUN_TEST(test_no_coprocessor_at_boot);
    RUN_TEST(test_identifies_then_writes_only_changed_control);
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
