#include "unity.h"
#include "rpm.h"
#include "sensors_cal.h"

void setUp(void) {}
void tearDown(void) {}

static uint16_t s_fake_rpm;
static uint16_t fake_get_rpm(void *ctx) { (void)ctx; return s_fake_rpm; }

static void test_classify_boundaries(void) {
    TEST_ASSERT_EQUAL_INT(RPM_ENGINE_NONE,    rpm_classify(0));
    TEST_ASSERT_EQUAL_INT(RPM_ENGINE_LOW,     rpm_classify(1));
    TEST_ASSERT_EQUAL_INT(RPM_ENGINE_LOW,     rpm_classify(ENGINE_RPM_LOW_LIMIT - 1)); /* 999 */
    TEST_ASSERT_EQUAL_INT(RPM_ENGINE_RUNNING, rpm_classify(ENGINE_RPM_LOW_LIMIT));     /* 1000 */
    TEST_ASSERT_EQUAL_INT(RPM_ENGINE_RUNNING, rpm_classify(5000));
}

static void test_read_from_source(void) {
    rpm_source_t src = { fake_get_rpm, 0 };
    s_fake_rpm = 2400;
    TEST_ASSERT_EQUAL_UINT16(2400, rpm_read(&src));
}

static void test_read_null_source_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT16(0, rpm_read(0));
    rpm_source_t empty = { 0, 0 };
    TEST_ASSERT_EQUAL_UINT16(0, rpm_read(&empty));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_classify_boundaries);
    RUN_TEST(test_read_from_source);
    RUN_TEST(test_read_null_source_is_zero);
    return UNITY_END();
}
