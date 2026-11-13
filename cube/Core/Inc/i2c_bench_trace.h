#ifndef I2C_BENCH_TRACE_H
#define I2C_BENCH_TRACE_H
#include <stdint.h>
/* Software wait stages, NOT bit-level bus observations. WAIT_END includes
 * the final data byte still in flight. READ_START includes first RX byte. */
enum {
    I2C_TRACE_ENTRY=1, I2C_TRACE_BUSY=2, I2C_TRACE_ADDR_W=3,
    I2C_TRACE_REGISTER=4, I2C_TRACE_READ_START=5, I2C_TRACE_DATA_TX=6,
    I2C_TRACE_DATA_RX=7, I2C_TRACE_WAIT_END=8, I2C_TRACE_RELOAD=9
};
void i2c_bench_stage(void *handle, uint16_t stage);
/* reason 0: hardware error bits, 1: software timeout. Called BEFORE cleanup. */
void i2c_bench_error(void *handle, uint32_t isr, uint16_t reason);
#ifdef HEATER_BENCH_FAULTS
int i2c_heater_bench_abort(void *handle);
#endif
#endif
