#include "is31fl3293.h"

static uint8_t s_normal_control = IS31_NORMAL_400HZ;

void is31_set_pwm_hz(uint16_t hz)
{
    s_normal_control = hz == 200u ? IS31_NORMAL_200HZ : IS31_NORMAL_400HZ;
}

uint16_t is31_pwm_hz(void)
{
    return s_normal_control == IS31_NORMAL_200HZ ? 200u : 400u;
}

static int wr(const i2c_backend_t *bus, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return bus->write(bus->ctx, reg, buf, len) == 0 ? 0 : -1;
}
static int wr1(const i2c_backend_t *bus, uint8_t reg, uint8_t v)
{
    return wr(bus, reg, &v, 1U);
}

uint16_t is31_read_byte(const i2c_backend_t *bus, uint8_t reg)
{
    uint8_t value;
    return bus->read(bus->ctx, reg, &value, 1U) == 0 ? value : IS31_READ_FAILED;
}

static void read_alignment(const i2c_backend_t *bus, uint16_t *values)
{
    if (!values) return;
    for (uint8_t reg = 0; reg <= 4; ++reg)
        values[reg] = is31_read_byte(bus, reg);
}

int is31_init(const i2c_backend_t *bus, uint16_t *reset_regs, uint16_t *configured_regs)
{
    static const uint8_t scaling[3] = { IS31_SCALING_FULL, IS31_SCALING_FULL, IS31_SCALING_FULL };
    static const uint8_t pwm0[6] = { 0, 0, 0, 0, 0, 0 };
    if (wr1(bus, IS31_REG_RESET, IS31_KEY) != 0) return -1;
    read_alignment(bus, reset_regs);
    if (wr1(bus, IS31_REG_SHUTDOWN, IS31_SHUTDOWN_OFF) != 0) return -1;
    if (wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL) != 0) return -1;
    if (wr1(bus, IS31_REG_CURRENT, IS31_CURRENT_VAL) != 0) return -1;
    if (wr(bus, IS31_REG_SCALING, scaling, 3U) != 0) return -1;
    if (wr1(bus, IS31_REG_CL_UPDATE, IS31_KEY) != 0) return -1;
    if (wr(bus, IS31_REG_PWM, pwm0, 6U) != 0) return -1;
    if (wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY) != 0) return -1;
    read_alignment(bus, configured_regs);
    return 0;
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

int is31_write_pwm_and_latch(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    if (write_pwm(bus, out1, out2, out3) != 0) return -1;
    return wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY);
}

int is31_set_pwm(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3)
{
    if (wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL) != 0) return -1;
    return is31_write_pwm_and_latch(bus, out1, out2, out3);
}

uint8_t is31_control_value(uint16_t out1, uint16_t out2, uint16_t out3)
{
    uint8_t enabled = (out1 ? 0x10u : 0u) | (out2 ? 0x20u : 0u) | (out3 ? 0x40u : 0u);
    return enabled ? (uint8_t)(s_normal_control | enabled) : IS31_SHUTDOWN_OFF;
}

int is31_enable_outputs(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3,
                       is31_enable_sample_t *sample)
{
    uint8_t control = is31_control_value(out1, out2, out3);
    if (!(control & 1u)) return wr1(bus, IS31_REG_SHUTDOWN, control);
    if (sample) {
        sample->control = control;
        sample->before_control = is31_read_byte(bus, IS31_REG_SHUTDOWN);
        sample->before_mode = is31_read_byte(bus, IS31_REG_CONFIG);
        sample->after_mode = IS31_NOT_READ;
        sample->result = 1; /* Enable write failed. */
    }
    if (wr1(bus, IS31_REG_SHUTDOWN, control) != 0) return -1;
    if (sample) sample->after_mode = is31_read_byte(bus, IS31_REG_CONFIG);
    int rc = wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL);
    if (sample) sample->result = rc == 0 ? 0 : 2;
    return rc;
}

static int check_pwm_mode(const i2c_backend_t *bus, uint16_t *mode)
{
    *mode = is31_read_byte(bus, IS31_REG_CONFIG);
    return *mode == IS31_CONFIG_VAL ? 0 : -1;
}

int is31_restore(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3,
                 is31_enable_sample_t *sample)
{
    static const uint8_t scaling[3] = { IS31_SCALING_FULL, IS31_SCALING_FULL, IS31_SCALING_FULL };
    is31_enable_sample_t unlogged = {0};
    if (!sample) sample = &unlogged;
    uint8_t control = is31_control_value(out1, out2, out3);
    int waking = 0;
    if (!(control & 1u)) {
        if (wr1(bus, IS31_REG_SHUTDOWN, IS31_SHUTDOWN_OFF) != 0) return -1;
    } else {
        sample->before_control = is31_read_byte(bus, IS31_REG_SHUTDOWN);
        if (sample->before_control == IS31_READ_FAILED) return -1;
        waking = !(sample->before_control & 1u);
    }
    if (waking) {
        sample->control = control;
        sample->before_mode = is31_read_byte(bus, IS31_REG_CONFIG);
        sample->after_mode = sample->disabled_mode = sample->cleared_mode =
            sample->current_mode = sample->pwm_mode = IS31_NOT_READ;
        sample->result = 3; /* Wake preparation failed. */
        if (wr1(bus, IS31_REG_SHUTDOWN, s_normal_control) != 0) return -1;
        sample->disabled_mode = is31_read_byte(bus, IS31_REG_CONFIG);
        if (is31_read_byte(bus, IS31_REG_SHUTDOWN) != s_normal_control) return -1;
    }
    if (wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL) != 0) return -1;
    if (waking && check_pwm_mode(bus, &sample->cleared_mode) != 0) return -1;
    if (wr1(bus, IS31_REG_CURRENT, IS31_CURRENT_VAL) != 0) return -1;
    if (wr(bus, IS31_REG_SCALING, scaling, 3U) != 0) return -1;
    if (wr1(bus, IS31_REG_CL_UPDATE, IS31_KEY) != 0) return -1;
    if (waking && check_pwm_mode(bus, &sample->current_mode) != 0) return -1;
    if (write_pwm(bus, out1, out2, out3) != 0) return -1;
    if (wr1(bus, IS31_REG_PWM_UPDATE, IS31_KEY) != 0) return -1;
    if (!waking) return is31_enable_outputs(bus, out1, out2, out3, sample);

    if (check_pwm_mode(bus, &sample->pwm_mode) != 0) return -1;
    sample->result = 1; /* Final enable write failed. */
    if (wr1(bus, IS31_REG_SHUTDOWN, control) != 0) return -1;
    sample->result = 4; /* Final mode did not verify. */
    if (check_pwm_mode(bus, &sample->after_mode) != 0) return -1;
    sample->result = 0;
    return 0;
}

int is31_shutdown(const i2c_backend_t *bus)
{
    /* Turn outputs off first, then restore PWM mode while shut down. */
    if (wr1(bus, IS31_REG_SHUTDOWN, IS31_SHUTDOWN_OFF) != 0) return -1;
    return wr1(bus, IS31_REG_CONFIG, IS31_CONFIG_VAL);
}
