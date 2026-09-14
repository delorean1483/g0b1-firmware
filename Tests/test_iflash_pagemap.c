#include "unity.h"
#include "iflash_pagemap.h"

void setUp(void){}
void tearDown(void){}

/* 2-bank part (FLASH_PAGE_NB = 128): the config on the EF-G0B1R. This is the
   case that bricked the unit -- flat page 128 (slot B first page) MUST map to
   bank 2 / page 0, NOT bank 1 / page 128 (which wraps to physical page 0). */
void test_2bank_lower_half_is_bank1(void){
    uint32_t bank, pnb;
    iflash_map_page(0, 128, &bank, &pnb);   TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(0, pnb);
    iflash_map_page(15, 128, &bank, &pnb);  TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(15, pnb);
    iflash_map_page(16, 128, &bank, &pnb);  TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(16, pnb);  /* slot A first */
    iflash_map_page(127, 128, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(127, pnb);
}
void test_2bank_upper_half_is_bank2(void){
    uint32_t bank, pnb;
    iflash_map_page(128, 128, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(1, bank); TEST_ASSERT_EQUAL_UINT32(0, pnb);   /* slot B first (was the brick) */
    iflash_map_page(239, 128, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(1, bank); TEST_ASSERT_EQUAL_UINT32(111, pnb); /* slot B last */
    iflash_map_page(240, 128, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(1, bank); TEST_ASSERT_EQUAL_UINT32(112, pnb); /* boot-config page 0 */
    iflash_map_page(241, 128, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(1, bank); TEST_ASSERT_EQUAL_UINT32(113, pnb); /* boot-config page 1 */
}
/* 1-bank part (FLASH_PAGE_NB = 256): everything is bank 1, flat == page. */
void test_1bank_all_bank1(void){
    uint32_t bank, pnb;
    iflash_map_page(0, 256, &bank, &pnb);   TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(0, pnb);
    iflash_map_page(128, 256, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(128, pnb);
    iflash_map_page(255, 256, &bank, &pnb); TEST_ASSERT_EQUAL_UINT32(0, bank); TEST_ASSERT_EQUAL_UINT32(255, pnb);
}

int main(void){
    UNITY_BEGIN();
    RUN_TEST(test_2bank_lower_half_is_bank1);
    RUN_TEST(test_2bank_upper_half_is_bank2);
    RUN_TEST(test_1bank_all_bank1);
    return UNITY_END();
}
