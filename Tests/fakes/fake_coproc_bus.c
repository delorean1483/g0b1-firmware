#include "fake_coproc_bus.h"
#include "heater.h"
#include "htr_i2c_map.h"
#include "htr_types.h"
#include <string.h>

static int fc_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    fake_coproc_t *f = (fake_coproc_t *)ctx;
    uint16_t i;
    f->reads++;
    if (f->fail || (f->fail_identity && reg == HTR_R_DEVICE_ID)) return -1;
    for (i = 0; i < len; i++) buf[i] = ((reg + i) < FAKE_COPROC_REGS) ? f->regs[reg + i] : 0xFFu;
    return 0;
}
static int fc_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    fake_coproc_t *f = (fake_coproc_t *)ctx;
    uint16_t i;
    f->writes++;
    if (f->fail) return -1;
    f->last_write_reg = reg;
    f->last_write_len = len;
    for (i = 0; i < len; i++) if ((reg + i) < FAKE_COPROC_REGS) f->regs[reg + i] = buf[i];
    if (reg == HTR_R_RUN_REQUEST && buf[0] == 0U)
        f->regs[HTR_R_FLAGS + 1U] |= (uint8_t)(HTR_F_START_ARMED >> 8);
    if (reg == HTR_R_RUN_REQUEST && len == 1U && buf[0] == 0U &&
        f->regs[HTR_R_MAP_VERSION] == 1U && f->regs[HTR_R_PHASE] == HTR_PHASE_RUNNING)
        f->regs[HTR_R_PHASE] = HTR_PHASE_COOLDOWN;
    if (reg == HTR_R_COMMAND && len != 0U) {
        f->command_writes++;
        if (f->fail_command_after_accept) return -1;
    }
    return 0;
}
void fake_coproc_init(fake_coproc_t *f)
{
    memset(f, 0, sizeof(*f));
    f->regs[HTR_R_DEVICE_ID] = HTR_DEVICE_ID;
    f->regs[HTR_R_MAP_VERSION] = HEATER_EXPECTED_MAP_VERSION;
    f->regs[HTR_R_HOST_TIMEOUT_S] = HTR_HOST_TIMEOUT_DEFAULT_S;
    f->regs[HTR_R_FLAGS + 1U] = (uint8_t)(HTR_F_START_ARMED >> 8);
    f->regs[HTR_R_HEATER_TYPE] = 0xFFu;
    f->api.read = fc_read;
    f->api.write = fc_write;
    f->api.ctx = f;
}
