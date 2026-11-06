/* drv_heater_i2c2.c -- I2C2 transport to the heater coprocessor (0x64).
 * Pure transport: register semantics live in heater-common/htr_i2c_map.h and
 * App/services/heater.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_heater_i2c2.h"
#include "htr_i2c_map.h"
#include "drv_i2c2_bus.h"
#include <stddef.h>


#define HEATER_I2C_TIMEOUT_MS 20u

static int hc_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_read(HTR_I2C_ADDR7, reg, buf, len, HEATER_I2C_TIMEOUT_MS);
}

static int hc_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return drv_i2c2_write(HTR_I2C_ADDR7, reg, buf, len, HEATER_I2C_TIMEOUT_MS);
}

static const i2c_backend_t s_backend = { hc_read, hc_write, NULL };

const i2c_backend_t *drv_heater_i2c2_backend(void) { return &s_backend; }
