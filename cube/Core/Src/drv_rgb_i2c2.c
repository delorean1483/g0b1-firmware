/* drv_rgb_i2c2.c -- I2C2 transport to the IS31FL3293 RGB driver (0x6C).
 * Pure transport: register semantics live in App/services/is31fl3293.h and
 * rgb.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_rgb_i2c2.h"
#include "is31fl3293.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c2;

#define RGB_I2C_TIMEOUT_MS 10u

static int rgb_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(IS31_ADDR7 << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len,
                             RGB_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int rgb_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(IS31_ADDR7 << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                              RGB_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static const i2c_backend_t s_backend = { rgb_read, rgb_write, NULL };

const i2c_backend_t *drv_rgb_i2c2_backend(void) { return &s_backend; }
