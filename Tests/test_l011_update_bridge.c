#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "drv_i2c2_bus.h"
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include "l011_update.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"
#include "rgb.h"

static bool boot, busy, running, lit, wrong_map, bus_failed, local_run, lamp_unverified;
static uint16_t mode_value;
static uint8_t phase;
static unsigned transfers, resets;
static uint8_t response[4] = { L011_BOOT_ID };

uint8_t heater_get_request(void) { return local_run; }
void heater_forget_coproc(void) { ++resets; }
void rgb_set_logical(uint8_t r, uint8_t g, uint8_t b) { assert(!(r | g | b)); }
void rgb_get_requested(uint8_t out[3]) { out[0] = lit; out[1] = out[2] = 0; }
void rgb_get_applied(uint8_t out[3]) { rgb_get_requested(out); }
uint16_t rgb_get_status(void) { return lamp_unverified ? 0U : RGB_ST_APPLY_OK; }

int drv_i2c2_read(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)timeout;
    if (bus_failed) return -1;
    if (address == L011_BOOT_ADDR && boot && !busy) {
        assert(reg == 0 && len == 4);
        memcpy(data, response, len);
        return 0;
    }
    if (address != HTR_I2C_ADDR7 || boot) return -1;
    if (reg == HTR_R_DEVICE_ID) {
        assert(len == 2);
        data[0] = HTR_DEVICE_ID;
        data[1] = wrong_map ? 1u : HTR_MAP_VERSION;
    } else {
        assert(len == 1);
        data[0] = reg == HTR_R_PHASE ? phase : running;
    }
    return 0;
}

int drv_i2c2_write_once(uint8_t address, uint8_t reg, const uint8_t *data,
                        uint16_t len, uint32_t timeout)
{
    (void)timeout;
    ++transfers;
    if (address == HTR_I2C_ADDR7) {
        assert(!boot && reg == L011_ENTER_REG && len == 2 && data[0] == L011_ENTER_KEY);
        boot = true;
    } else {
        assert(boot && address == L011_BOOT_ADDR && reg == L011_COMMIT && len == 2);
        memcpy(response + 2, data, 2);
        response[1] = 1;
    }
    return -1; /* lost ACK after delivery */
}

static modbus_exc_t heater_mode(uint16_t reg, uint16_t *value)
{
    assert(reg == 10);
    *value = mode_value;
    return MB_EXC_NONE;
}

static uint16_t rejection(void)
{
    uint16_t code;
    assert(mb_reg_read(MB_L011_UPDATE_REJECT, &code) == MB_EXC_NONE);
    return code;
}

static void expect_rejection(uint16_t code)
{
    assert(mb_reg_write(MB_L011_UPDATE_MODE, 0xB011) == MB_EXC_ILLEGAL_VALUE);
    assert(rejection() == code);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    const uint16_t mode = MB_L011_UPDATE_MODE;
    uint16_t value = 0;
    mb_reg_reset();
    mb_reg_bind(10, heater_mode, 0);
    drv_l011_update_register();
    assert(rejection() == 0);
    assert(mb_reg_write(MB_L011_UPDATE_REJECT, 1u) == MB_EXC_ILLEGAL_ADDRESS);
    lit = true;
    expect_rejection(3);
    lit = false;
    lamp_unverified = true;
    expect_rejection(3);
    lamp_unverified = false;
    running = true;
    expect_rejection(2);
    running = false;
    local_run = true;
    expect_rejection(2);
    local_run = false;
    phase = HTR_PHASE_DETECTING;
    expect_rejection(1);
    phase = HTR_PHASE_OFF;
    wrong_map = true;
    expect_rejection(4);
    wrong_map = false;
    mode_value = 1;
    expect_rejection(5);
    mode_value = 0;
    bus_failed = true;
    expect_rejection(6);
    bus_failed = false;
    assert(!drv_l011_update_active() && !transfers);
    assert(mb_reg_write(mode, 0xB011) == MB_EXC_NONE);
    assert(drv_l011_update_active() && boot && transfers == 1 && resets == 1);
    assert(rejection() == 0);
    assert(mb_reg_write(mode, 0xB012) != MB_EXC_NONE);
    assert(mb_reg_write(mode + 2, 3) == MB_EXC_NONE);
    assert(mb_reg_write(mode, 0xB014) != MB_EXC_NONE);
    uint8_t cmd = L011_COMMIT;
    uint16_t crc = modbus_crc16(&cmd, 1);
    assert(mb_reg_write(mode + 3, cmd | ((crc & 255u) << 8)) == MB_EXC_NONE);
    assert(mb_reg_write(mode + 4, crc >> 8) == MB_EXC_NONE);
    assert(mb_reg_write(mode, 0xB014) == MB_EXC_NONE && transfers == 2);
    busy = true;
    assert(mb_reg_write(mode, 0xB013) == MB_EXC_NONE);
    assert(mb_reg_read(mode + 1, &value) == MB_EXC_NONE && value == 0);
    busy = false;
    assert(mb_reg_write(mode, 0xB013) == MB_EXC_NONE);
    assert(mb_reg_read(mode + 1, &value) == MB_EXC_NONE && value == 0x101);
    boot = false;
    wrong_map = true;
    assert(mb_reg_write(mode, 0xB012) == MB_EXC_ILLEGAL_VALUE);
    assert(drv_l011_update_active() && resets == 1);
    wrong_map = false;
    assert(mb_reg_write(mode, 0xB013) == MB_EXC_NONE);
    assert(mb_reg_read(mode + 1, &value) == MB_EXC_NONE && value == 0x200);
    assert(mb_reg_write(mode, 0xB012) == MB_EXC_NONE);
    assert(!drv_l011_update_active() && resets == 2);
    assert(mb_reg_read(mode + 1, &value) == MB_EXC_NONE && value == 0x200);
    assert(mb_reg_write(mode, 0xB013) == MB_EXC_ILLEGAL_VALUE);
    assert(mb_reg_read(MB_L011_UPDATE_LAST, &value) == MB_EXC_NONE);
    assert(mb_reg_read(MB_L011_UPDATE_LAST + 1u, &value) == MB_EXC_ILLEGAL_ADDRESS);

    /* Recover a session when G0B1 starts with the L011 already in its loader. */
    boot = true;
    assert(mb_reg_write(mode, 0xB011) == MB_EXC_NONE);
    assert(drv_l011_update_active() && transfers == 2 && resets == 3);
    boot = false;
    assert(mb_reg_write(mode, 0xB012) == MB_EXC_NONE && resets == 4);
    puts("Bridge: phase OFF, lamp/run interlocks, lost ACK, loader re-entry and map-v2 exit passed.");
    return 0;
}
