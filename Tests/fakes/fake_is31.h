#ifndef FAKE_IS31_H
#define FAKE_IS31_H
/* i2c_backend_t over a 0x40-byte register image of an IS31FL3293, with
   failure injection, an attempt counter and an ordered write log. */
#include "i2c_backend.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t reg; uint8_t len; uint8_t data[8]; } fake_is31_xfer_t;
typedef struct {
    i2c_backend_t api;
    uint8_t  regs[0x40];
    bool     fail;            /* every transfer returns -1, nothing logged */
    unsigned attempts;        /* every read/write call, failed or not */
    fake_is31_xfer_t log[32]; unsigned nlog;   /* successful writes, in order */
    unsigned pwm_latches;     /* successful writes of 0xC5 to 0x28 */
} fake_is31_t;

void fake_is31_init(fake_is31_t *f);
#endif
