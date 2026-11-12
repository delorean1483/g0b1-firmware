#ifndef FAKE_COPROC_BUS_H
#define FAKE_COPROC_BUS_H
/* i2c_backend_t over a plain 0x46-byte register image of the heater
   coprocessor, with failure injection and a write log. */
#include "i2c_backend.h"
#include <stdbool.h>
#include <stdint.h>

#define FAKE_COPROC_REGS 0x46u
typedef struct {
    i2c_backend_t api;
    uint8_t regs[FAKE_COPROC_REGS];
    bool fail;
    bool fail_identity;
    bool fail_command_after_accept;
    unsigned command_writes;
    unsigned reads, writes;
    uint8_t last_write_reg;
    uint16_t last_write_len;
} fake_coproc_t;

/* Identity bytes preloaded (DEVICE_ID 0xEF, MAP_VERSION 1). */
void fake_coproc_init(fake_coproc_t *f);
#endif
