/* drv_rgb_i2c2.c -- I2C2 transport to the IS31FL3293 RGB driver (0x6C).
 * Pure transport: register semantics live in App/services/is31fl3293.h and
 * rgb.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_rgb_i2c2.h"
#include "is31fl3293.h"
#include "drv_i2c2_bus.h"
#include "bench_i2c2_profile.h"
#include <stddef.h>


#define RGB_I2C_TIMEOUT_MS BENCH_I2C2_TIMEOUT_MS

static int rgb_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_read(IS31_ADDR7, reg, buf, len, RGB_I2C_TIMEOUT_MS);
}

static int rgb_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_write(IS31_ADDR7, reg, buf, len, RGB_I2C_TIMEOUT_MS);
}

static const i2c_backend_t s_backend = { rgb_read, rgb_write, NULL };

const i2c_backend_t *drv_rgb_i2c2_backend(void) { return &s_backend; }
