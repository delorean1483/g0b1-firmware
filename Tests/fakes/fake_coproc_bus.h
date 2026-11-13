#ifndef FAKE_COPROC_BUS_H
#define FAKE_COPROC_BUS_H
/* i2c_backend_t over a plain register image of the heater
   coprocessor, with failure injection and a write log. */
#include "i2c_backend.h"
#include "htr_i2c_map.h"
#include <stdbool.h>
#include <stdint.h>

#define FAKE_COPROC_REGS HTR_REG_COUNT
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
#ifdef HEATER_BENCH_FAULTS
    uint16_t midbyte_mode;
#endif
} fake_coproc_t;

/* Identity bytes preloaded (DEVICE_ID 0xEF, MAP_VERSION = HTR_MAP_VERSION). */
void fake_coproc_init(fake_coproc_t *f);
#endif
