#include "bsp_io.h"

static const bsp_io_backend_t *s_be;

void bsp_io_init(const bsp_io_backend_t *be) { s_be = be; }
void bsp_out_set(bsp_out_t out, bool on) { if (s_be && s_be->out_set) s_be->out_set(s_be->ctx, (uint8_t)out, on); }
bool bsp_out_get(bsp_out_t out) { return (s_be && s_be->out_get) ? s_be->out_get(s_be->ctx, (uint8_t)out) : false; }
bool bsp_in_read(bsp_in_t in) { return (s_be && s_be->in_read) ? s_be->in_read(s_be->ctx, (uint8_t)in) : false; }
