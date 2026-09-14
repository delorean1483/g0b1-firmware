#include "unity.h"
#include "app_confirm.h"
#include "bootcfg.h"
#include "crc32.h"
#include "fake_iflash.h"
#include <string.h>

static const iflash_backend_t *FL;

static void seed_trial_B(void) {
    /* program a tiny valid image into slot B, mark it TRIAL-active */
    uint8_t img[16]; for (int i=0;i<16;i++) img[i]=(uint8_t)(i+3);
    FL->program(FL->ctx, SLOT_B_BASE, img, 16);
    bootcfg_t c; memset(&c,0,sizeof c); c.magic=BOOTCFG_MAGIC; c.version=BOOTCFG_VERSION;
    c.active_slot=SLOT_B; c.slot[SLOT_B].state=SLOT_STATE_TRIAL;
    c.slot[SLOT_B].length=16; c.slot[SLOT_B].crc32=crc32_compute(fake_iflash_ptr(SLOT_B_BASE),16);
    c.trial_count=2;
    bootcfg_save(FL,&c);
}
void setUp(void){ fake_iflash_reset(); FL=fake_iflash_backend(); seed_trial_B(); app_confirm_init(FL); }
void tearDown(void){}

void test_armed_when_trial(void){ TEST_ASSERT_EQUAL_INT(1, app_confirm_is_armed()); }
void test_commits_after_healthy_window(void){
    for (int i=0;i<APP_CONFIRM_HEALTHY_SECS;i++) app_confirm_tick(1);
    TEST_ASSERT_EQUAL_INT(0, app_confirm_is_armed());
    bootcfg_t r; bootcfg_load(FL,&r);
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_COMMITTED, r.slot[SLOT_B].state);
    TEST_ASSERT_EQUAL_UINT16(0, r.trial_count);
}
void test_unhealthy_resets_counter(void){
    for (int i=0;i<APP_CONFIRM_HEALTHY_SECS-1;i++) app_confirm_tick(1);
    app_confirm_tick(0);                       /* blip resets */
    app_confirm_tick(1);
    TEST_ASSERT_EQUAL_INT(1, app_confirm_is_armed());  /* still not committed */
}
void test_noop_when_already_committed(void){
    bootcfg_t c; bootcfg_load(FL,&c); c.slot[SLOT_B].state=SLOT_STATE_COMMITTED; bootcfg_save(FL,&c);
    app_confirm_init(FL);
    TEST_ASSERT_EQUAL_INT(0, app_confirm_is_armed());
}

int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_armed_when_trial); RUN_TEST(test_commits_after_healthy_window);
    RUN_TEST(test_unhealthy_resets_counter); RUN_TEST(test_noop_when_already_committed);
    return UNITY_END();
}
