#ifndef FAKE_I2C_H
#define FAKE_I2C_H
#include "i2c_backend.h"
#define FAKE_I2C_REG_COUNT 256u
void     fake_i2c_init(i2c_backend_t *be); /* wire be to the fake; zero all regs */
void     fake_i2c_reset(void);             /* zero all regs */
uint8_t *fake_i2c_raw(void);               /* backing register array */
#endif
