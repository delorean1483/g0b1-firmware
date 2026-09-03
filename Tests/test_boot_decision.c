#include "unity.h"
#include "boot_decision.h"
#include <string.h>

/* Fake slot oracle: bit0 = slot A ok, bit1 = slot B ok. */
static int g_ok_mask;
static int fake_ok(void *v, slot_id_t s) { (void)v; return (g_ok_mask >> (int)s) & 1; }

static bootcfg_t base_cfg(void) {
    bootcfg_t c; memset(&c, 0, sizeof c);
    c.magic = BOOTCFG_MAGIC; c.version = BOOTCFG_VERSION; c.active_slot = SLOT_A;
    c.slot[SLOT_A].state = SLOT_STATE_COMMITTED; c.slot[SLOT_A].length = 100; c.slot[SLOT_A].crc32 = 1;
    c.slot[SLOT_B].state = SLOT_STATE_BAD;
    return c;
}
void setUp(void) { g_ok_mask = 0x1; }
void tearDown(void) {}

void test_enter_magic_wins(void) {
    bootcfg_t c = base_cfg();
    boot_decision_t d = boot_decide(&c, 1, BOOT_MAGIC_ENTER, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_ENTER_UPDATE, d.action);
    TEST_ASSERT_EQUAL_INT(0, d.cfg_dirty);
}
void test_invalid_cfg_recovers(void) {
    bootcfg_t c = base_cfg();
    boot_decision_t d = boot_decide(&c, 0, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_ENTER_UPDATE, d.action);
    TEST_ASSERT_EQUAL_INT(0, d.cfg_dirty);
}
void test_normal_boot_active(void) {
    bootcfg_t c = base_cfg();
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_JUMP_SLOT, d.action);
    TEST_ASSERT_EQUAL_INT(SLOT_A, d.slot);
    TEST_ASSERT_EQUAL_INT(0, d.cfg_dirty);
}
void test_active_bad_falls_to_other(void) {
    bootcfg_t c = base_cfg();
    c.slot[SLOT_B].state = SLOT_STATE_COMMITTED; c.slot[SLOT_B].length = 100; c.slot[SLOT_B].crc32 = 2;
    g_ok_mask = 0x2;                              /* only B ok */
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_JUMP_SLOT, d.action);
    TEST_ASSERT_EQUAL_INT(SLOT_B, d.slot);
    TEST_ASSERT_EQUAL_INT(1, d.cfg_dirty);
    TEST_ASSERT_EQUAL_UINT16(SLOT_B, c.active_slot);
}
void test_trial_increments_and_boots(void) {
    bootcfg_t c = base_cfg();
    c.slot[SLOT_A].state = SLOT_STATE_TRIAL; c.trial_count = 0;
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_JUMP_SLOT, d.action);
    TEST_ASSERT_EQUAL_UINT16(1, c.trial_count);
    TEST_ASSERT_EQUAL_INT(1, d.cfg_dirty);
}
void test_trial_exceeds_limit_reverts_to_committed_other(void) {
    bootcfg_t c = base_cfg();
    c.slot[SLOT_A].state = SLOT_STATE_TRIAL; c.trial_count = TRIAL_BOOT_LIMIT; /* this boot pushes it over */
    c.slot[SLOT_B].state = SLOT_STATE_COMMITTED; c.slot[SLOT_B].length = 100; c.slot[SLOT_B].crc32 = 2;
    g_ok_mask = 0x3;
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_JUMP_SLOT, d.action);
    TEST_ASSERT_EQUAL_INT(SLOT_B, d.slot);
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_BAD, c.slot[SLOT_A].state);
    TEST_ASSERT_EQUAL_UINT16(SLOT_B, c.active_slot);
}
void test_trial_exceeds_no_fallback_recovers(void) {
    bootcfg_t c = base_cfg();
    c.slot[SLOT_A].state = SLOT_STATE_TRIAL; c.trial_count = TRIAL_BOOT_LIMIT;
    c.slot[SLOT_B].state = SLOT_STATE_BAD;
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_ENTER_UPDATE, d.action);
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_BAD, c.slot[SLOT_A].state);
    TEST_ASSERT_EQUAL_INT(1, d.cfg_dirty);
}
void test_both_invalid_recovers(void) {
    bootcfg_t c = base_cfg(); g_ok_mask = 0x0;
    boot_decision_t d = boot_decide(&c, 1, 0, fake_ok, 0);
    TEST_ASSERT_EQUAL_INT(BOOT_ACTION_ENTER_UPDATE, d.action);
    TEST_ASSERT_EQUAL_INT(0, d.cfg_dirty);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_magic_wins); RUN_TEST(test_invalid_cfg_recovers);
    RUN_TEST(test_normal_boot_active); RUN_TEST(test_active_bad_falls_to_other);
    RUN_TEST(test_trial_increments_and_boots); RUN_TEST(test_trial_exceeds_limit_reverts_to_committed_other);
    RUN_TEST(test_trial_exceeds_no_fallback_recovers); RUN_TEST(test_both_invalid_recovers);
    return UNITY_END();
}
