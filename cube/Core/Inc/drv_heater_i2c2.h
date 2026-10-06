#ifndef DRV_HEATER_I2C2_H
#define DRV_HEATER_I2C2_H
/* I2C2 (PA11 SCL / PB14 SDA, 100 kHz) backend for the heater service: a
   register-pointer i2c_backend_t bound to the heater coprocessor at
   HTR_I2C_ADDR7 (0x64). Same HAL_I2C_Mem_* pattern as drv_mcp7940n.c. */
#include "i2c_backend.h"

const i2c_backend_t *drv_heater_i2c2_backend(void);

#endif /* DRV_HEATER_I2C2_H */
