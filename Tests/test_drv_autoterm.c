#include "drv_autoterm.h"
#include "autoterm_proto.h"
#include "fake_htr_uart.h"
#include "unity.h"
#include <string.h>

static fake_uart_t u;
static const htr_driver_t *d = &drv_autoterm;
static uint8_t hs_major = 0, hs_minor = 1;   /* simulated heater state */
static bool heater_alive = true;
static size_t answered;                       /* writes already answered */

static void heater_reply(uint8_t cmd, const uint8_t *p, uint8_t len){
    uint8_t f[AT_MAX_FRAME]; size_t n = AT_BuildFrame(AT_DIR_RESP, cmd, p, len, f);
    fake_uart_inject(&u, f, n);
}
/* Simulated heater: answers version only at 9600, status at 9600. */
static void respond(void){
    for (; answered < u.writes; answered++) {
        if (!heater_alive || u.write_baud[answered] != 9600u) continue;
        uint8_t cmd = fake_uart_write_cmd(&u, answered);
        if (cmd == AT_CMD_VERSION) { const uint8_t v[5]={2,1,3,5,3}; heater_reply(cmd, v, 5); }
        if (cmd == AT_CMD_STATUS) {
            uint8_t s[19]={0}; s[0]=hs_major; s[1]=hs_minor; s[3]=29; s[4]=0x7F; s[6]=126; s[7]=1; s[8]=0x2E;
            heater_reply(cmd, s, 19);
        }
    }
}
static uint32_t run(uint32_t from, uint32_t to, bool probing, htr_probe_t *last){
    htr_probe_t r = HTR_PROBE_BUSY;
    for (uint32_t t = from; t < to; t += 10u) {
        if (probing) r = d->probe(d->ctx, t); else d->process(d->ctx, t);
        respond();
        if (probing && r != HTR_PROBE_BUSY) { if (last) *last = r; return t; }
    }
    if (last) *last = r;
    return to;
}
static size_t count_cmd(uint8_t cmd, size_t from){
    size_t n=0; for(size_t i=from;i<u.writes;i++) if(fake_uart_write_cmd(&u,i)==cmd) n++; return n;
}
static size_t last_index_of(uint8_t cmd){
    size_t r=(size_t)-1; for(size_t i=0;i<u.writes;i++) if(fake_uart_write_cmd(&u,i)==cmd) r=i; return r;
}
void setUp(void){ fake_uart_init(&u); hs_major=0; hs_minor=1; heater_alive=true; answered=0; d->init(d->ctx,&u.api,0U); }
void tearDown(void){}

void test_identity(void){
    TEST_ASSERT_EQUAL_UINT8(HTR_TYPE_AUTOTERM, d->type_id);
    TEST_ASSERT_EQUAL_UINT8(HTR_WIRING_4W, d->wiring);
    TEST_ASSERT_TRUE(d->caps & HTR_CAP_SETPOINT);
    TEST_ASSERT_FALSE(d->caps & HTR_CAP_LEVEL);
}
void test_wake_sequence_bauds_counts_and_gaps(void){
    heater_alive = false;
    run(0, 9000, true, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(41, (int)u.writes);
    for (size_t i = 0; i < 35; i++) {
        TEST_ASSERT_EQUAL_HEX8(AT_CMD_WAKE, fake_uart_write_cmd(&u, i));
        TEST_ASSERT_EQUAL_UINT32(2400u, u.write_baud[i]);
    }
    for (size_t i = 35; i < 40; i++) {
        TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fake_uart_write_cmd(&u, i));
        TEST_ASSERT_EQUAL_UINT32(4800u, u.write_baud[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(AT_CMD_VERSION, fake_uart_write_cmd(&u, 40));
    TEST_ASSERT_EQUAL_UINT32(9600u, u.write_baud[40]);
}
void test_probe_found_when_version_answers_at_9600(void){
    htr_probe_t r; uint32_t t = run(0, 20000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, r);
    TEST_ASSERT_LESS_THAN_UINT32(9000u, t);
}
void test_probe_absent_after_three_wakes_and_sends_only_wake_or_version(void){
    heater_alive = false;
    htr_probe_t r; run(0, 60000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_ABSENT, r);
    TEST_ASSERT_EQUAL(3u*AT_WAKE_COUNT, count_cmd(AT_CMD_WAKE, 0));
    for (size_t i = 0; i < u.writes; i++) {
        uint8_t c = fake_uart_write_cmd(&u, i);
        TEST_ASSERT_TRUE(c == AT_CMD_WAKE || c == AT_CMD_VERSION);
    }
}
static uint32_t go_online(void){
    htr_probe_t r; uint32_t t = run(0, 20000, true, &r);
    TEST_ASSERT_EQUAL_INT(HTR_PROBE_FOUND, r); return t + 10u;
}
void test_online_polls_status_settings_and_temp_only_when_known(void){
    uint32_t t = go_online(); size_t from = u.writes;
    run(t, t + 20000, false, NULL);
    size_t st = count_cmd(AT_CMD_STATUS, from);
    TEST_ASSERT_TRUE(st >= 19 && st <= 21);
    size_t se = count_cmd(AT_CMD_SETTINGS, from);
    TEST_ASSERT_TRUE(se >= 3 && se <= 5);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_TEMP, from));      /* ambient unknown */
    htr_request_t rq = {0, 1, 20, 231};                     /* 23.1 C */
    d->apply(d->ctx, &rq); from = u.writes;
    run(t + 20000, t + 31000, false, NULL);
    size_t i = last_index_of(AT_CMD_TEMP);
    TEST_ASSERT_TRUE(i != (size_t)-1 && i >= from);
    TEST_ASSERT_EQUAL_UINT8(23, u.tx[u.write_off[i] + 5]);
}
void test_start_sends_captured_frame_until_heater_leaves_standby(void){
    uint32_t t = go_online();
    htr_request_t rq = {1, 1, 29, HTR_AMBIENT_UNKNOWN};
    d->start(d->ctx, &rq); size_t from = u.writes;
    run(t, t + 3000, false, NULL);                          /* heater still in 0.1 */
    size_t n1 = count_cmd(AT_CMD_START, from);
    TEST_ASSERT_TRUE(n1 >= 2 && n1 <= 4);                   /* retried ~1/s */
    size_t i = last_index_of(AT_CMD_START);
    const uint8_t st29[]={0xAA,0x03,0x07,0x00,0x01,0xFF,0xFF,0x02,0x1D,0x01,0xFF,0x11,0x19,0x34};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(st29, &u.tx[u.write_off[i]], 14);
    hs_major = 1; hs_minor = 0; from = u.writes;
    run(t + 3000, t + 8000, false, NULL);
    TEST_ASSERT_LESS_OR_EQUAL(1, (int)count_cmd(AT_CMD_START, from)); /* at most one in flight */
    from = u.writes; run(t + 8000, t + 12000, false, NULL);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_START, from));
}
void test_stop_repeats_until_cooldown_then_keeps_polling(void){
    uint32_t t = go_online(); hs_major = 2; hs_minor = 4;
    d->stop(d->ctx); size_t from = u.writes;
    run(t, t + 3000, false, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(2, (int)count_cmd(AT_CMD_STOP, from));
    hs_major = 4; hs_minor = 0; run(t + 3000, t + 5000, false, NULL);
    from = u.writes; run(t + 5000, t + 10000, false, NULL);
    TEST_ASSERT_EQUAL(0, count_cmd(AT_CMD_STOP, from));
    TEST_ASSERT_GREATER_OR_EQUAL(4, (int)count_cmd(AT_CMD_STATUS, from));
    htr_status_t s; d->status(d->ctx, 0, &s);
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_COOLDOWN, s.hint);
}
void test_setpoint_change_while_running_sends_settings_write(void){
    uint32_t t = go_online();
    htr_request_t rq = {1, 1, 22, HTR_AMBIENT_UNKNOWN};
    d->start(d->ctx, &rq); hs_major = 2; hs_minor = 4;
    run(t, t + 3000, false, NULL);
    rq.setpoint_c = 25; d->apply(d->ctx, &rq); size_t from = u.writes;
    run(t + 3000, t + 4000, false, NULL);
    size_t found = 0;
    for (size_t i = from; i < u.writes; i++)
        if (fake_uart_write_cmd(&u, i) == AT_CMD_SETTINGS && u.write_len[i] == 13) {
            TEST_ASSERT_EQUAL_UINT8(25, u.tx[u.write_off[i] + 8]); found++;
        }
    TEST_ASSERT_EQUAL(1, found);
}
void test_status_valid_bits_only_verified_fields(void){
    uint32_t t = go_online(); run(t, t + 2000, false, NULL);
    htr_status_t s; d->status(d->ctx, 0, &s);
    TEST_ASSERT_TRUE(s.any_rx);
    TEST_ASSERT_EQUAL_UINT8(HTR_HINT_OFF, s.hint);
    TEST_ASSERT_EQUAL_UINT8(0, s.vendor_state); TEST_ASSERT_EQUAL_UINT8(1, s.vendor_sub);
    TEST_ASSERT_EQUAL_UINT16(12600, s.supply_mv);
    TEST_ASSERT_EQUAL_HEX16(0x012E, s.exch_raw);
    TEST_ASSERT_EQUAL_HEX16(HTR_V_SUPPLY_MV | HTR_V_EXCH_RAW, s.valid);
}
void test_link_lost_rewakes_at_2400(void){
    uint32_t t = go_online(); heater_alive = false; size_t from = u.writes;
    run(t, t + 12000, false, NULL);
    size_t i = last_index_of(AT_CMD_WAKE);
    TEST_ASSERT_TRUE(i != (size_t)-1 && i >= from);
    TEST_ASSERT_EQUAL_UINT32(2400u, u.write_baud[i]);
}
int main(void){ UNITY_BEGIN();
    RUN_TEST(test_identity);
    RUN_TEST(test_wake_sequence_bauds_counts_and_gaps);
    RUN_TEST(test_probe_found_when_version_answers_at_9600);
    RUN_TEST(test_probe_absent_after_three_wakes_and_sends_only_wake_or_version);
    RUN_TEST(test_online_polls_status_settings_and_temp_only_when_known);
    RUN_TEST(test_start_sends_captured_frame_until_heater_leaves_standby);
    RUN_TEST(test_stop_repeats_until_cooldown_then_keeps_polling);
    RUN_TEST(test_setpoint_change_while_running_sends_settings_write);
    RUN_TEST(test_status_valid_bits_only_verified_fields);
    RUN_TEST(test_link_lost_rewakes_at_2400);
    return UNITY_END(); }
