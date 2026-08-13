#include "fake_bsp_io.h"

static bool s_out[OUT_COUNT];
static bool s_in[IN_COUNT];

static void fi_out_set(void *ctx, uint8_t out, bool on) { (void)ctx; if (out < OUT_COUNT) s_out[out] = on; }
static bool fi_out_get(void *ctx, uint8_t out) { (void)ctx; return (out < OUT_COUNT) ? s_out[out] : false; }
static bool fi_in_read(void *ctx, uint8_t in) { (void)ctx; return (in < IN_COUNT) ? s_in[in] : false; }

void fake_bsp_io_init(bsp_io_backend_t *be) {
    for (int i = 0; i < OUT_COUNT; i++) s_out[i] = false;
    for (int i = 0; i < IN_COUNT; i++) s_in[i] = false;
    be->out_set = fi_out_set; be->out_get = fi_out_get; be->in_read = fi_in_read; be->ctx = 0;
}
void fake_bsp_io_set_input(bsp_in_t in, bool level) { if (in < IN_COUNT) s_in[in] = level; }
bool fake_bsp_io_out(bsp_out_t out) { return (out < OUT_COUNT) ? s_out[out] : false; }
