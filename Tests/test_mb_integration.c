#include "unity.h"
#include "mb_engine.h"
#include "mb_regmodel.h"
#include "mbp_sensors.h"
#include "mbp_rtc.h"
#include "mbp_nvm.h"
#include "mbp_sys.h"
#include "sensors.h"
#include "sensors_cal.h"
#include "rtc.h"
#include "nvm.h"
#include "modbus_crc.h"
#include "fake_i2c.h"
#include "fake_nor.h"

static i2c_backend_t i2c;
static nvm_backend_t nor;
static uint16_t s_rpm;
static uint16_t fake_rpm(void *c) { (void)c; return s_rpm; }
static rpm_source_t src = { fake_rpm, 0 };
static void noop_reset(void) {}

void setUp(void) {
    mb_reg_reset(); mb_engine_init();
    sensors_init(VREF_CAL_DEFAULT, 0);
    fake_i2c_init(&i2c); rtc_init(&i2c);
    fake_nor_init(&nor); nvm_init(&nor);
    mbp_sensors_register(&src);
    mbp_rtc_register();
    mbp_nvm_register();
    mbp_sys_register(noop_reset);
}
void tearDown(void) {}

static void put_crc(uint8_t *b, uint16_t n) { uint16_t c = modbus_crc16(b, n); b[n]=(uint8_t)c; b[n+1]=(uint8_t)(c>>8); }

static void test_read_battery_over_the_wire(void) {
    for (int i = 0; i < SENS_AVG_DEFAULT; i++) sensors_add_sample(SENS_BATT, 2374); /* 12.00 V */
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_HOLDING, 0x00, 0x05, 0x00, 0x01 };  /* wire 5 -> reg 6 */
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(2, resp[2]);
    TEST_ASSERT_EQUAL_UINT16(1200, (uint16_t)(resp[3] << 8) | resp[4]);
}

static void test_write_setting_then_read_back_over_the_wire(void) {
    /* Write reg 14 (climate temp) = 70 via FC 0x06 (wire addr 13), then read it back via FC 0x03. */
    uint8_t w[8] = { MB_SLAVE_ADDR, MB_FC_WRITE_SINGLE, 0x00, 0x0D, 0x00, 0x46 };
    put_crc(w, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(w, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);                  /* echo, not exception */

    uint8_t r[8] = { MB_SLAVE_ADDR, MB_FC_READ_HOLDING, 0x00, 0x0D, 0x00, 0x01 };
    put_crc(r, 6);
    mb_engine_process(r, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT16(70, (uint16_t)(resp[3] << 8) | resp[4]);
}

static void test_unbound_control_register_exception_over_wire(void) {
    /* reg 10 (op-mode, M6) is unbound -> exception 0x02. wire addr 9. */
    uint8_t req[8] = { MB_SLAVE_ADDR, MB_FC_READ_HOLDING, 0x00, 0x09, 0x00, 0x01 };
    put_crc(req, 6);
    uint8_t resp[MB_MAX_FRAME]; uint16_t rl = 0;
    mb_engine_process(req, 8, resp, &rl);
    TEST_ASSERT_EQUAL_UINT8(MB_FC_READ_HOLDING | MB_ERROR_RESPONSE, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(MB_EXC_ILLEGAL_ADDRESS, resp[2]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_battery_over_the_wire);
    RUN_TEST(test_write_setting_then_read_back_over_the_wire);
    RUN_TEST(test_unbound_control_register_exception_over_wire);
    return UNITY_END();
}
