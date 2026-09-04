#include "wk2132.h"
#include "fake_wk2132_i2c.h"
#include "unity.h"

static WK2132_t dev;

void setUp(void)
{
    fake_wk2132_reset();
    TEST_ASSERT_EQUAL(WK2132_OK,
        WK2132_Init(&dev, fake_wk2132_i2c(), 0U, true, true));
}
void tearDown(void) {}

void test_base_address_from_dip(void)
{ /* A1=1,A0=1 -> 0x70 base */
    TEST_ASSERT_EQUAL_HEX8(0x70U, dev.base_addr_7bit);
}

void test_begin_sets_4800_divisor(void)
{
    TEST_ASSERT_EQUAL(WK2132_OK, WK2132_Begin4800_8N1(&dev));
    TEST_ASSERT_EQUAL_UINT16(191U, fake_wk2132_divisor());
}

void test_begin_not_detected_when_gena_clear(void)
{
    fake_wk2132_set_gena_present(false);
    TEST_ASSERT_EQUAL(WK2132_NOT_DETECTED, WK2132_Begin4800_8N1(&dev));
}

void test_write_pushes_frame_to_fifo(void)
{
    (void)WK2132_Begin4800_8N1(&dev);
    uint8_t f[16] = {0xAA, 0x66, 0x02, 0x0B, 0, 0, 0, 0,
                      1, 0x06, 0x02, 0, 0, 0, 0, 0x16};
    TEST_ASSERT_EQUAL(WK2132_OK, WK2132_Write(&dev, f, sizeof f));
    TEST_ASSERT_EQUAL_UINT(16U, fake_wk2132_tx_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(f, fake_wk2132_tx_data(), 16);
}

void test_read_returns_injected_rx(void)
{
    (void)WK2132_Begin4800_8N1(&dev);
    uint8_t inj[3] = {0xAA, 0x77, 0x02};
    fake_wk2132_inject_rx(inj, 3);
    uint8_t buf[8];
    size_t n = 0;
    TEST_ASSERT_EQUAL(WK2132_OK, WK2132_Read(&dev, buf, sizeof buf, &n));
    TEST_ASSERT_EQUAL_UINT(3U, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(inj, buf, 3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_base_address_from_dip);
    RUN_TEST(test_begin_sets_4800_divisor);
    RUN_TEST(test_begin_not_detected_when_gena_clear);
    RUN_TEST(test_write_pushes_frame_to_fifo);
    RUN_TEST(test_read_returns_injected_rx);
    return UNITY_END();
}
