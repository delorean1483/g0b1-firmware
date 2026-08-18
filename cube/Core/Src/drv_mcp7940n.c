/* drv_mcp7940n.c — I2C1 implementation of i2c_backend_t (Task 7).
 *
 * MCP7940N battery-backed RTCC on I2C1 (PB6 SCL / PB7 SDA), 7-bit slave
 * address 0x6F. The portable rtc service (rtc.c / rtc_calendar.c, host-tested
 * by test_rtc / test_mbp_rtc) owns all register semantics — BCD, ST/VBATEN,
 * OSCRUN, SRAM base 0x20, RTCC regs. This driver is pure register-addressed
 * I2C transport: read()/write() move `len` bytes starting at register `reg`.
 *
 * HAL_I2C_Mem_Read/Write handle the START / control-byte / repeated-START
 * addressing sequence, so the 8-bit register pointer maps 1:1 onto the
 * i2c_backend_t contract. Both return 0 on success, non-zero on HAL
 * error/timeout (the rtc service treats any non-zero as a failed access).
 *
 * Bench bring-up plan Task 7:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "drv_mcp7940n.h"
#include "main.h"      /* CubeMX HAL: hi2c1, HAL_I2C_* */

extern I2C_HandleTypeDef hi2c1;   /* CubeMX-generated (MX_I2C1_Init) */

/* MCP7940N 7-bit slave address 0x6F, shifted to the 8-bit form HAL expects
 * (HAL OR-s in the R/W bit itself). */
#define MCP_ADDR_7BIT   0x6Fu
#define MCP_ADDR_8BIT   ((uint16_t)(MCP_ADDR_7BIT << 1))   /* 0xDE */

#define I2C_TMO_MS      100u      /* per-transfer HAL timeout */

static int mcp_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c1, MCP_ADDR_8BIT, reg,
                                            I2C_MEMADD_SIZE_8BIT, buf, len,
                                            I2C_TMO_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static int mcp_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(&hi2c1, MCP_ADDR_8BIT, reg,
                                             I2C_MEMADD_SIZE_8BIT,
                                             (uint8_t *)buf, len, I2C_TMO_MS);
    return (st == HAL_OK) ? 0 : -1;
}

static const i2c_backend_t s_backend = {
    .read  = mcp_read,
    .write = mcp_write,
    .ctx   = NULL,
};

const i2c_backend_t *drv_mcp7940n_backend(void) { return &s_backend; }

int drv_mcp7940n_probe(void)
{
    /* 3 tries, per-try timeout — confirms the part ACKs its address on the bus. */
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c1, MCP_ADDR_8BIT, 3u,
                                                 I2C_TMO_MS);
    return (st == HAL_OK) ? 0 : -1;
}
