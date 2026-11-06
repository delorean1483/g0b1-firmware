#ifndef DRV_I2C2_BUS_H
#define DRV_I2C2_BUS_H
#include <stdint.h>
/* Shared blocking superloop-only transport. At most one clear per second.
 * Initialize once before either client starts; does not touch hardware. */
void drv_i2c2_init(void);
int drv_i2c2_read(uint8_t addr7, uint8_t reg, uint8_t *buf,
                  uint16_t len, uint32_t timeout_ms);
int drv_i2c2_write(uint8_t addr7, uint8_t reg, const uint8_t *buf,
                   uint16_t len, uint32_t timeout_ms);
/* Side-effecting writes: recover on eligible errors, but never replay. */
int drv_i2c2_write_once(uint8_t addr7, uint8_t reg, const uint8_t *buf,
                        uint16_t len, uint32_t timeout_ms);
/* Read-only Modbus: 80 attempts, 81 clears, 82 failures, 83 last line levels
 * (bit0 SCL, bit1 SDA). Counters saturate and clear on MCU reset. */
void drv_i2c2_register_diagnostics(void);
#endif
