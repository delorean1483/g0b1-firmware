#ifndef DRV_I2C2_BUS_H
#define DRV_I2C2_BUS_H
#include <stdint.h>
typedef struct {
    uint32_t error, isr, tick;
    uint16_t status, lines;
    uint8_t attempted;
} drv_i2c2_result_t;
const drv_i2c2_result_t *drv_i2c2_last_result(void);
/* Shared blocking superloop-only transport. At most one clear per second.
 * Initialize once before either client starts; does not touch hardware. */
uint16_t drv_i2c2_snapshot(uint32_t *tick);
void drv_i2c2_init(void);
uint32_t drv_i2c2_recovery_epoch(void);
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
