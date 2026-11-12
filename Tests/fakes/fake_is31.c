#include "fake_is31.h"
#include <string.h>

#define FAKE_IS31_REGS 0x40u
#define FAKE_IS31_LOG (sizeof(f->log) / sizeof(f->log[0]))
#define FAKE_IS31_LATCH_REG 0x28u
#define FAKE_IS31_KEY 0xC5u

static int fi_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    fake_is31_t *f = (fake_is31_t *)ctx;
    uint16_t i;
    f->attempts++;
    if (f->fail_next_read) { f->fail_next_read = false; return -1; }
    if (f->fail || f->fail_read) return -1;
    for (i = 0; i < len; i++) buf[i] = ((unsigned)reg + i) < FAKE_IS31_REGS ? f->regs[reg + i] : 0xFFu;
    if (reg == 1 && len == 1 && f->corrupt_read_control) buf[0] = 0x75;
    if (reg == 2 && len == 1 && f->corrupt_read_mode) buf[0] = 0x35;
    return 0;
}
static int pwm_nonzero(const uint8_t *b, uint16_t n)
{
    while (n--) if (*b++) return 1;
    return 0;
}
static int fi_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    fake_is31_t *f = (fake_is31_t *)ctx;
    uint16_t i;
    f->attempts++;
    if (f->corrupt_next_write) {
        f->corrupt_next_write = false;
        f->regs[1] = 0x6B;
        f->regs[2] = 0x35;
        return -1;
    }
    if (f->fail || (f->fail_pwm && reg == 0x19u && pwm_nonzero(buf, len))) return -1;
    if (f->nlog < FAKE_IS31_LOG) {
        fake_is31_xfer_t *x = &f->log[f->nlog];
        x->reg = reg;
        x->len = (uint8_t)len;
        memset(x->data, 0, sizeof(x->data));
        memcpy(x->data, buf, len < sizeof(x->data) ? len : sizeof(x->data));
        f->nlog++;
    }
    bool was_shutdown = !(f->regs[1] & 1u);
    for (i = 0; i < len; i++) if (((unsigned)reg + i) < FAKE_IS31_REGS) f->regs[reg + i] = buf[i];
    if (reg == 1 && was_shutdown && (buf[0] & 1) && f->mode_on_enable)
        f->regs[2] = buf[0] & 0x3Fu;
    if (reg == FAKE_IS31_LATCH_REG && len >= 1U && buf[0] == FAKE_IS31_KEY) f->pwm_latches++;
    return 0;
}
void fake_is31_init(fake_is31_t *f)
{
    memset(f, 0, sizeof(*f));
    f->api.read = fi_read;
    f->api.write = fi_write;
    f->api.ctx = f;
}

