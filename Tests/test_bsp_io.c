#include "unity.h"
#include "bsp_io.h"
#include "fake_bsp_io.h"

static bsp_io_backend_t be;
void setUp(void) { fake_bsp_io_init(&be); bsp_io_init(&be); }
void tearDown(void) {}

static void test_out_set_get_roundtrip(void) {
    TEST_ASSERT_FALSE(bsp_out_get(OUT_FUEL_PUMP));
    bsp_out_set(OUT_FUEL_PUMP, true);
    TEST_ASSERT_TRUE(bsp_out_get(OUT_FUEL_PUMP));
    TEST_ASSERT_TRUE(fake_bsp_io_out(OUT_FUEL_PUMP)); /* recorded in the fake */
    bsp_out_set(OUT_FUEL_PUMP, false);
    TEST_ASSERT_FALSE(bsp_out_get(OUT_FUEL_PUMP));
}

static void test_outputs_independent(void) {
    bsp_out_set(OUT_STARTER, true);
    bsp_out_set(OUT_CONDENSER_FAN, true);
    TEST_ASSERT_TRUE(bsp_out_get(OUT_STARTER));
    TEST_ASSERT_TRUE(bsp_out_get(OUT_CONDENSER_FAN));
    TEST_ASSERT_FALSE(bsp_out_get(OUT_GLOW_PLUG));    /* untouched */
}

static void test_input_read(void) {
    TEST_ASSERT_FALSE(bsp_in_read(IN_OIL_PRESSURE));
    fake_bsp_io_set_input(IN_OIL_PRESSURE, true);
    TEST_ASSERT_TRUE(bsp_in_read(IN_OIL_PRESSURE));
    TEST_ASSERT_FALSE(bsp_in_read(IN_TRUCK_IGNITION)); /* independent */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_out_set_get_roundtrip);
    RUN_TEST(test_outputs_independent);
    RUN_TEST(test_input_read);
    return UNITY_END();
}
