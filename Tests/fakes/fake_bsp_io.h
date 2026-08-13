#ifndef FAKE_BSP_IO_H
#define FAKE_BSP_IO_H
#include "bsp_io_backend.h"
#include "board_pins.h"
void fake_bsp_io_init(bsp_io_backend_t *be);      /* wire fake; all outputs off, inputs low */
void fake_bsp_io_set_input(bsp_in_t in, bool level);
bool fake_bsp_io_out(bsp_out_t out);              /* recorded relay state */
#endif /* FAKE_BSP_IO_H */
