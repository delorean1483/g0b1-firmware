#ifndef DRV_RGB_I2C2_H
#define DRV_RGB_I2C2_H
/* I2C2 (PA11 SCL / PB14 SDA, 100 kHz) backend for the external RGB lamp
   service: a register-pointer i2c_backend_t bound to the IS31FL3293 at
   IS31_ADDR7 (0x6C). Same HAL_I2C_Mem_* pattern as drv_heater_i2c2.c. */
#include "i2c_backend.h"

const i2c_backend_t *drv_rgb_i2c2_backend(void);
void drv_rgb_i2c2_probe_register(void);
void drv_rgb_i2c2_probe_process(uint32_t now);

#endif /* DRV_RGB_I2C2_H */
