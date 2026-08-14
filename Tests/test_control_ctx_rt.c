#include "unity.h"
#include "control.h"
#include <string.h>

static apu_ctx_t ctx;
void setUp(void) { memset(&ctx, 0xFF, sizeof(ctx)); control_init(&ctx); }
void tearDown(void) {}

static void test_init_resets_runtime_fields(void) {
    TEST_ASSERT_EQUAL_UINT8(0, ctx.machine_run_min);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.engine_run_min);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.engine_oil_min);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_resets_runtime_fields);
    return UNITY_END();
}
