#ifndef BSP_IO_H
#define BSP_IO_H
#include "types.h"
#include "board_pins.h"
#include "bsp_io_backend.h"
void bsp_io_init(const bsp_io_backend_t *be);
void bsp_out_set(bsp_out_t out, bool on);
bool bsp_out_get(bsp_out_t out);
bool bsp_in_read(bsp_in_t in);
#endif /* BSP_IO_H */
