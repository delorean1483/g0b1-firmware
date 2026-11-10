#include "is31fl3293.h"

static int wr(const i2c_backend_t *bus, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return bus->write(bus->ctx, reg, buf, len) == 0 ? 0 : -1;
}
static int wr1(const i2c_backend_t *bus, uint8_t reg, uint8_t v)
{
    return wr(bus, reg, &v, 1U);
}

int is31_init(const i2c_backend_t *bus)
{
    static const uint8_t scaling[3] = { IS31_SCALING_FULL, IS31_SCALING_FULL, IS31_SCALING_FULL };
    static const uint8_t pwm0[6] = { 0, 0, 0, 0, 0, 0 };
    if (wr1(bus, IS31_REG_RESET, IS31_KEY) != 0) return -1;
    if (wr1(bus, IS31_REG_SHUTDOWN, IS31_SHUTDOWN_OFF) != 0) return -1;
    if (wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL) != 0) return -1;
    if (wr1(bus, IS31_REG_CURRENT, IS31_CURRENT_VAL) != 0) return -1;
    if (wr(bus, IS31_REG_SCALING, scaling, 3U) != 0) return -1;
    if (wr1(bus, IS31_REG_CL_UPDATE, IS31_KEY) != 0) return -1;
    if (wr(bus, IS31_REG_PWM, pwm0, 6U) != 0) return -1;
    return wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY);
}

static int write_pwm(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    uint16_t v[3];
    uint8_t b[6];
    unsigned i;
    v[0] = out1; v[1] = out2; v[2] = out3;
    for (i = 0; i < 3U; i++) {
        if (v[i] > IS31_PWM_MAX) v[i] = IS31_PWM_MAX;
        b[2U * i] = (uint8_t)(v[i] & 0xFFu);
        b[2U * i + 1U] = (uint8_t)(v[i] >> 8);
    }
    return wr(bus, IS31_REG_PWM, b, 6U);
}

int is31_set_pwm(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    if (write_pwm(bus, out1, out2, out3) != 0) return -1;
    return wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY);
}

int is31_enable_outputs(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    uint8_t enabled = (out1 ? 0x10u : 0u) | (out2 ? 0x20u : 0u) | (out3 ? 0x40u : 0u);
    return enabled ? wr1(bus, IS31_REG_SHUTDOWN, IS31_NORMAL_400HZ | enabled)
                   : is31_shutdown(bus);
}

int is31_restore(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    static const uint8_t scaling[3] = { IS31_SCALING_FULL, IS31_SCALING_FULL, IS31_SCALING_FULL };
    if (!(out1 || out2 || out3)) return is31_shutdown(bus);
    if (wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL) != 0) return -1;
    if (wr1(bus, IS31_REG_CURRENT, IS31_CURRENT_VAL) != 0) return -1;
    if (wr(bus, IS31_REG_SCALING, scaling, 3U) != 0) return -1;
    if (wr1(bus, IS31_REG_CL_UPDATE, IS31_KEY) != 0) return -1;
    if (write_pwm(bus, out1, out2, out3) != 0) return -1;
    if (wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY) != 0) return -1;
    return is31_enable_outputs(bus, out1, out2, out3);
}

int is31_shutdown(const i2c_backend_t *bus)
{
    return wr1(bus, IS31_REG_SHUTDOWN, IS31_SHUTDOWN_OFF);
}
