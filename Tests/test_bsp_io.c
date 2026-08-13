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

static void test_null_backend_is_safe(void) {
    bsp_io_init(0);                              /* no backend wired */
    bsp_out_set(OUT_FUEL_PUMP, true);            /* must not crash */
    TEST_ASSERT_FALSE(bsp_out_get(OUT_FUEL_PUMP));   /* returns false, no deref */
    TEST_ASSERT_FALSE(bsp_in_read(IN_OIL_PRESSURE));
}

static void test_out_of_range_index_is_safe(void) {
    bsp_out_set((bsp_out_t)OUT_COUNT, true);     /* index == OUT_COUNT: fake must reject */
    TEST_ASSERT_FALSE(bsp_out_get((bsp_out_t)OUT_COUNT));  /* returns false, no OOB read */
    TEST_ASSERT_FALSE(bsp_in_read((bsp_in_t)IN_COUNT));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_out_set_get_roundtrip);
    RUN_TEST(test_outputs_independent);
    RUN_TEST(test_input_read);
    RUN_TEST(test_null_backend_is_safe);
    RUN_TEST(test_out_of_range_index_is_safe);
    return UNITY_END();
}
