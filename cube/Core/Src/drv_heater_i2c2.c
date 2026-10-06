/* drv_heater_i2c2.c -- I2C2 transport to the heater coprocessor (0x64).
 * Pure transport: register semantics live in heater-common/htr_i2c_map.h and
 * App/services/heater.c. Returns 0 on success, -1 on HAL error/timeout. */
#include "drv_heater_i2c2.h"
#include "htr_i2c_map.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c2;

#define HEATER_I2C_TIMEOUT_MS 10u

static int hc_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(HTR_I2C_ADDR7 << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len,
                             HEATER_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int hc_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    return (HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(HTR_I2C_ADDR7 << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                              HEATER_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static const i2c_backend_t s_backend = { hc_read, hc_write, NULL };

const i2c_backend_t *drv_heater_i2c2_backend(void) { return &s_backend; }
