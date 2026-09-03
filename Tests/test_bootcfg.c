#include "unity.h"
#include "bootcfg.h"
#include "crc32.h"
#include "fake_iflash.h"
#include <string.h>

static const iflash_backend_t *FL;
void setUp(void) { fake_iflash_reset(); FL = fake_iflash_backend(); }
void tearDown(void) {}

void test_load_fresh_device_fails(void) {
    bootcfg_t c; TEST_ASSERT_NOT_EQUAL(0, bootcfg_load(FL, &c));
}
void test_save_then_load_roundtrip(void) {
    bootcfg_t c; memset(&c, 0, sizeof c);
    c.magic = BOOTCFG_MAGIC; c.version = BOOTCFG_VERSION; c.active_slot = SLOT_A; c.seq = 0;
    c.slot[SLOT_A].state = SLOT_STATE_COMMITTED; c.slot[SLOT_A].length = 8; c.slot[SLOT_A].crc32 = 0x1234;
    TEST_ASSERT_EQUAL_INT(0, bootcfg_save(FL, &c));
    bootcfg_t r; TEST_ASSERT_EQUAL_INT(0, bootcfg_load(FL, &r));
    TEST_ASSERT_EQUAL_UINT16(SLOT_A, r.active_slot);
    TEST_ASSERT_EQUAL_UINT32(c.seq, r.seq);           /* seq was bumped to 1 by save */
    TEST_ASSERT_EQUAL_UINT32(1u, r.seq);
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_COMMITTED, r.slot[SLOT_A].state);
}
void test_newest_seq_wins_pingpong(void) {
    bootcfg_t c; memset(&c, 0, sizeof c); c.magic = BOOTCFG_MAGIC; c.version = BOOTCFG_VERSION;
    c.active_slot = SLOT_A; c.seq = 0; bootcfg_save(FL, &c);   /* -> page0, seq 1 */
    c.active_slot = SLOT_B;            bootcfg_save(FL, &c);   /* -> page1, seq 2 */
    c.active_slot = SLOT_A;            bootcfg_save(FL, &c);   /* -> page0, seq 3 */
    bootcfg_t r; bootcfg_load(FL, &r);
    TEST_ASSERT_EQUAL_UINT32(3u, r.seq);
    TEST_ASSERT_EQUAL_UINT16(SLOT_A, r.active_slot);
}
void test_corrupt_record_ignored(void) {
    bootcfg_t c; memset(&c, 0, sizeof c); c.magic = BOOTCFG_MAGIC; c.version = BOOTCFG_VERSION;
    bootcfg_save(FL, &c);                                     /* good record on page0, seq1 */
    /* Corrupt page0's magic by erasing it; load must fail (only page has bad magic). */
    FL->erase_page(FL->ctx, BOOTCFG_PAGE_0);
    bootcfg_t r; TEST_ASSERT_NOT_EQUAL(0, bootcfg_load(FL, &r));
}
void test_slot_crc_ok(void) {
    /* program 16 bytes into slot A, compute its crc, store in cfg, expect OK. */
    uint8_t img[16]; for (int i=0;i<16;i++) img[i]=(uint8_t)(i*7+1);
    FL->program(FL->ctx, SLOT_A_BASE, img, 16);
    bootcfg_t c; memset(&c,0,sizeof c); c.magic=BOOTCFG_MAGIC; c.version=BOOTCFG_VERSION;
    c.slot[SLOT_A].state = SLOT_STATE_COMMITTED; c.slot[SLOT_A].length = 16;
    c.slot[SLOT_A].crc32 = crc32_compute(fake_iflash_ptr(SLOT_A_BASE), 16);
    TEST_ASSERT_EQUAL_INT(1, bootcfg_slot_crc_ok(FL, &c, SLOT_A));
    c.slot[SLOT_A].crc32 ^= 1u;                              /* wrong crc */
    TEST_ASSERT_EQUAL_INT(0, bootcfg_slot_crc_ok(FL, &c, SLOT_A));
    c.slot[SLOT_A].crc32 ^= 1u; c.slot[SLOT_A].state = SLOT_STATE_BAD; /* BAD short-circuits */
    TEST_ASSERT_EQUAL_INT(0, bootcfg_slot_crc_ok(FL, &c, SLOT_A));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_load_fresh_device_fails); RUN_TEST(test_save_then_load_roundtrip);
    RUN_TEST(test_newest_seq_wins_pingpong); RUN_TEST(test_corrupt_record_ignored);
    RUN_TEST(test_slot_crc_ok);
    return UNITY_END();
}
