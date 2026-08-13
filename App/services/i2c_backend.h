#ifndef I2C_BACKEND_H
#define I2C_BACKEND_H
#include <stdint.h>
/* Abstract I2C device: register-addressed access to a single fixed device
   (the MCP7940N). read()/write() move `len` bytes starting at register `reg`.
   Both return 0 on success, non-zero on error. The concrete HAL implementation
   (I2C1) is deferred to hardware bring-up. */
typedef struct i2c_backend {
    int  (*read)(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len);
    int  (*write)(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len);
    void *ctx;
} i2c_backend_t;
#endif /* I2C_BACKEND_H */
