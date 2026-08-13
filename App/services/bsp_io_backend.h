#ifndef BSP_IO_BACKEND_H
#define BSP_IO_BACKEND_H
#include "types.h"
/* Abstract relay-output + digital-input hardware. Concrete HAL (GPIO/ULN2003)
   deferred to bench. */
typedef struct bsp_io_backend {
    void (*out_set)(void *ctx, uint8_t out, bool on);
    bool (*out_get)(void *ctx, uint8_t out);
    bool (*in_read)(void *ctx, uint8_t in);
    void *ctx;
} bsp_io_backend_t;
#endif /* BSP_IO_BACKEND_H */
