/* drv_wk2132_i2c2.c -- I2C2 implementation of wk2132_i2c_t (Task 5).
 *
 * DFR0627 (WK2132 dual UART-over-I2C bridge) on I2C2 (PA11 SCL / PB14 SDA).
 * The portable wk2132 service (App/services/wk2132.c, host-tested by
 * Tests/test_wk2132.c) owns all WK2132 register/FIFO semantics and computes
 * the 7-bit device address (base + channel/object bits, per the DFR0627 DIP
 * switches). This driver is pure I2C2 transport: it shifts that 7-bit
 * address into the 8-bit form HAL expects and calls the matching HAL
 * primitive. Every op returns 0 on success, non-zero on HAL error/timeout
 * (the wk2132 service treats any non-zero as a failed access and counts it
 * in dev->i2c_errors).
 *
 * mem_read/mem_write hit WK2132 registers (SPAGE/SCR/LCR/FCR/BAUD.../...)
 * via HAL_I2C_Mem_*, matching drv_mcp7940n.c's pattern. tx/rx move raw UART
 * FIFO bytes with no register pointer, so they go through
 * HAL_I2C_Master_Transmit/Receive instead.
 *
 * hi2c2 does not exist until the CubeMX I2C2 enable step is applied
 * (PA11=I2C2_SCL, PB14=I2C2_SDA, open-drain, 100 kHz) -- see
 * docs/vevor-heater.md (Task 7) for the exact config. This file only
 * declares `extern I2C_HandleTypeDef hi2c2;`; it does not create it.
 *
 * Bench bring-up plan Task 7:
 *   .superpowers/sdd/2026-09-03-vevor-heater-firmware/task-7-brief.md
 */
#include "drv_wk2132_i2c2.h"
#include "main.h"      /* CubeMX HAL: hi2c2 (post I2C2 enable), HAL_I2C_* */

extern I2C_HandleTypeDef hi2c2;   /* CubeMX-generated (MX_I2C2_Init), pending .ioc I2C2 enable */

#define WK2132_I2C_TIMEOUT_MS   10u      /* per-transfer HAL timeout */

static int wk2132_mem_read(void *ctx, uint8_t addr7, uint8_t reg,
                            uint8_t *buf, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(addr7 << 1),
                                            reg, I2C_MEMADD_SIZE_8BIT, buf,
                                            len, WK2132_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static int wk2132_mem_write(void *ctx, uint8_t addr7, uint8_t reg,
                             const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(addr7 << 1),
                                             reg, I2C_MEMADD_SIZE_8BIT,
                                             (uint8_t *)buf, len,
                                             WK2132_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static int wk2132_tx(void *ctx, uint8_t addr7, const uint8_t *buf,
                      uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c2,
                                                   (uint16_t)(addr7 << 1),
                                                   (uint8_t *)buf, len,
                                                   WK2132_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static int wk2132_rx(void *ctx, uint8_t addr7, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Master_Receive(&hi2c2,
                                                  (uint16_t)(addr7 << 1),
                                                  buf, len,
                                                  WK2132_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static const wk2132_i2c_t s_backend = {
    .mem_read  = wk2132_mem_read,
    .mem_write = wk2132_mem_write,
    .tx        = wk2132_tx,
    .rx        = wk2132_rx,
    .ctx       = NULL,
};

const wk2132_i2c_t *drv_wk2132_i2c2_backend(void) { return &s_backend; }
