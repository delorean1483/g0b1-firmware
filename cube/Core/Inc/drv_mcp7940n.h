#ifndef DRV_MCP7940N_H
#define DRV_MCP7940N_H

/* Concrete I2C1 backend for the portable rtc service (Task 7).
 *
 * MCP7940N battery-backed RTCC on I2C1 (PB6 SCL / PB7 SDA), 7-bit slave
 * address 0x6F. Implements i2c_backend_t as register-addressed I2C transport
 * (HAL_I2C_Mem_Read/Write, 8-bit register pointer). All register/BCD/oscillator
 * semantics live in the portable rtc service, not here.
 *
 * Bench bring-up plan Task 7:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "i2c_backend.h"

/* Factory: hand this to rtc_init(). */
const i2c_backend_t *drv_mcp7940n_backend(void);

/* Bench aid: probe the bus for the MCP7940N (HAL_I2C_IsDeviceReady at 0x6F).
 * Returns 0 if the part ACKs its address, non-zero otherwise. Use to confirm
 * the RTC is alive/wired before trusting a time read. */
int drv_mcp7940n_probe(void);

#endif /* DRV_MCP7940N_H */
