#include "fake_i2c.h"
#include <string.h>

static uint8_t s_regs[FAKE_I2C_REG_COUNT];

static int fi_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len) {
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) buf[i] = s_regs[(uint8_t)(reg + i)];
    return 0;
}
static int fi_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len) {
    (void)ctx;
    for (uint16_t i = 0; i < len; i++) s_regs[(uint8_t)(reg + i)] = buf[i];
    /* Model the oscillator: RTCSEC(0x00) ST bit -> RTCWKDAY(0x03) OSCRUN bit. */
    if (reg == 0x00 && len >= 1) {
        if (buf[0] & 0x80) s_regs[0x03] |= 0x20;
        else               s_regs[0x03] &= (uint8_t)~0x20;
    }
    return 0;
}
void fake_i2c_reset(void) { memset(s_regs, 0, sizeof s_regs); }
uint8_t *fake_i2c_raw(void) { return s_regs; }
void fake_i2c_init(i2c_backend_t *be) {
    fake_i2c_reset();
    be->read = fi_read;
    be->write = fi_write;
    be->ctx = 0;
}
