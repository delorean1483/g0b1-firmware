/* drv_rgb_i2c2.c -- I2C2 transport to the IS31FL3293 RGB driver (0x6C).
 * Pure transport: register semantics live in App/services/is31fl3293.h and
 * rgb.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_rgb_i2c2.h"
#include "is31fl3293.h"
#include "drv_i2c2_bus.h"
#include "rgb.h"
#include "mb_regmodel.h"
#include "main.h"
#include <string.h>
#include <stddef.h>


#define RGB_I2C_TIMEOUT_MS 20u   /* whole transfer; the 54-byte heater read is ~6-7 ms at 100 kHz */

static int rgb_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_read(IS31_ADDR7, reg, buf, len, RGB_I2C_TIMEOUT_MS);
}

static int rgb_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_write_once(IS31_ADDR7, reg, buf, len, RGB_I2C_TIMEOUT_MS);
}

static const i2c_backend_t s_backend = { rgb_read, rgb_write, NULL };

const i2c_backend_t *drv_rgb_i2c2_backend(void) { return &s_backend; }

/* Bounded bench experiment. Never changes the requested lamp color or heater.
 * Reg 0x04 affects pattern hold only; each write requires mode 0 readback. */
static struct {
    uint16_t state, value, seconds, interval, reason;
    uint16_t calls, ok, arlo, nack, timeout, busy, deferred, other, attempted;
    uint16_t verified, verify_errors, mode_errors, control_errors;
    uint16_t expected, last_mode, last_control, recovery_before[3];
    uint8_t color[3];
    uint8_t clock_started;
    uint32_t start, next, end, repair_at, elapsed;
} s_probe;

static void probe_increment(uint16_t *v) { if (*v != UINT16_MAX) ++*v; }

static int heater_is_off(void)
{
    uint16_t run, mode;
    return mb_reg_read(53, &run) == MB_EXC_NONE && run == 0 &&
           mb_reg_read(10, &mode) == MB_EXC_NONE && mode == 0;
}

static modbus_exc_t probe_write(uint16_t reg, uint16_t value)
{
    if (reg == 108 && value == 0) {
        if (s_probe.state == 1) { s_probe.state = 3; s_probe.reason = 1; }
        return MB_EXC_NONE;
    }
    if (s_probe.state == 1) return MB_EXC_SLAVE_DEVICE_FAILURE;
    if (reg == 109 && value >= 1 && value <= 120) {
        s_probe.seconds = value; return MB_EXC_NONE;
    }
    if (reg == 110 && value >= 10 && value <= 1000) {
        s_probe.interval = value; return MB_EXC_NONE;
    }
    if (reg != 108 || (value != 1 && value != 2)) return MB_EXC_ILLEGAL_VALUE;
    uint8_t requested[3], applied[3];
    rgb_get_requested(requested); rgb_get_applied(applied);
    if (!heater_is_off() || !(rgb_get_status() & RGB_ST_APPLY_OK) ||
        memcmp(requested, applied, 3) != 0) return MB_EXC_SLAVE_DEVICE_FAILURE;
    uint16_t seconds = s_probe.seconds, interval = s_probe.interval;
    memset(&s_probe, 0, sizeof(s_probe));
    s_probe.seconds = seconds; s_probe.interval = interval;
    s_probe.value = value == 1 ? 1 : 0;
    memcpy(s_probe.color, applied, 3);
    s_probe.expected = is31_control_value(applied[1], applied[0], applied[2]);
    for (uint16_t i = 0; i < 3; ++i)
        (void)mb_reg_read((uint16_t)(80 + i), &s_probe.recovery_before[i]);
    s_probe.state = 1; /* process supplies the clock at the next superloop pass */
    return MB_EXC_NONE;
}

static modbus_exc_t probe_read(uint16_t reg, uint16_t *out)
{
    switch (reg) {
    case 108: *out = 0; break;
    case 109: *out = s_probe.seconds; break;
    case 110: *out = s_probe.interval; break;
    case 111: *out = s_probe.state; break;
    case 112: *out = s_probe.value; break;
    case 113: *out = s_probe.reason; break;
    case 114: *out = s_probe.calls; break;
    case 115: *out = s_probe.ok; break;
    case 116: *out = s_probe.arlo; break;
    case 117: *out = s_probe.nack; break;
    case 118: *out = s_probe.timeout; break;
    case 119: *out = s_probe.busy; break;
    case 120: *out = s_probe.deferred; break;
    case 121: *out = s_probe.other; break;
    case 122: *out = s_probe.verified; break;
    case 123: *out = s_probe.verify_errors; break;
    case 124: *out = s_probe.mode_errors; break;
    case 125: *out = s_probe.control_errors; break;
    case 126: *out = s_probe.expected; break;
    case 127: *out = s_probe.last_mode; break;
    case 128: *out = s_probe.last_control; break;
    case 129: *out = (uint16_t)s_probe.elapsed; break;
    case 130: *out = (uint16_t)(s_probe.elapsed >> 16); break;
    case 131: case 132: case 133: {
        uint16_t current;
        (void)mb_reg_read((uint16_t)(80 + reg - 131), &current);
        *out = (uint16_t)(current - s_probe.recovery_before[reg - 131]);
        break;
    }
    case 134: *out = s_probe.attempted; break;
    default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    return MB_EXC_NONE;
}

void drv_rgb_i2c2_probe_register(void)
{
    memset(&s_probe, 0, sizeof(s_probe));
    s_probe.seconds = 60; s_probe.interval = 50;
    for (uint16_t reg = 108; reg <= 134; ++reg)
        mb_reg_bind(reg, probe_read, reg <= 110 ? probe_write : 0);
}

void drv_rgb_i2c2_probe_process(uint32_t now)
{
    if (s_probe.state != 1) return;
    if (!s_probe.clock_started) {
        s_probe.clock_started = 1;
        s_probe.start = s_probe.next = s_probe.repair_at = now;
        s_probe.end = now + (uint32_t)s_probe.seconds * 1000u;
    }
    s_probe.elapsed = now - s_probe.start;
    if ((int32_t)(now - s_probe.end) >= 0) { s_probe.state = 2; return; }
    if ((int32_t)(now - s_probe.next) < 0) return;
    s_probe.next = now + s_probe.interval;
    uint8_t requested[3], applied[3];
    rgb_get_requested(requested); rgb_get_applied(applied);
    if (!heater_is_off()) { s_probe.state = 3; s_probe.reason = 2; return; }
    if (memcmp(requested, s_probe.color, 3) != 0) {
        s_probe.state = 3; s_probe.reason = 3; return;
    }
    if (!(rgb_get_status() & RGB_ST_APPLY_OK) || memcmp(applied, s_probe.color, 3) != 0) {
        probe_increment(&s_probe.verify_errors); return;
    }
    uint8_t mode, control;
    if (rgb_read(NULL, IS31_REG_CONFIG, &mode, 1) != 0 ||
        rgb_read(NULL, IS31_REG_SHUTDOWN, &control, 1) != 0) {
        probe_increment(&s_probe.verify_errors); return;
    }
    s_probe.last_mode = mode; s_probe.last_control = control;
    if (mode != 0 || control != s_probe.expected) {
        if (mode != 0) probe_increment(&s_probe.mode_errors);
        if (control != s_probe.expected) probe_increment(&s_probe.control_errors);
        /* Ask the normal service to repair, at most once per second. */
        if ((int32_t)(now - s_probe.repair_at) >= 0) {
            rgb_set_logical(requested[0], requested[1], requested[2]);
            s_probe.repair_at = now + 1000u;
        }
        return;
    }
    probe_increment(&s_probe.verified);
    probe_increment(&s_probe.calls);
    uint8_t value = (uint8_t)s_probe.value;
    int rc = drv_i2c2_write_once(IS31_ADDR7, 0x04, &value, 1, RGB_I2C_TIMEOUT_MS);
    const drv_i2c2_result_t *result = drv_i2c2_last_result();
    if (result->attempted) probe_increment(&s_probe.attempted);
    else { probe_increment(&s_probe.deferred); return; }
    if (rc == 0) { probe_increment(&s_probe.ok); return; }
    if (result->error & HAL_I2C_ERROR_ARLO) probe_increment(&s_probe.arlo);
    if (result->error & HAL_I2C_ERROR_AF) probe_increment(&s_probe.nack);
    if (result->status == HAL_TIMEOUT || (result->error & HAL_I2C_ERROR_TIMEOUT))
        probe_increment(&s_probe.timeout);
    if (result->status == HAL_BUSY) probe_increment(&s_probe.busy);
    if (result->status == HAL_ERROR &&
        !(result->error & (HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_AF | HAL_I2C_ERROR_TIMEOUT)))
        probe_increment(&s_probe.other);
}
