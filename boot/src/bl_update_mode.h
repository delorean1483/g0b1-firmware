#ifndef BL_UPDATE_MODE_H
#define BL_UPDATE_MODE_H
#include "iflash_backend.h"
#include "bootcfg.h"

/* Update-mode receive/dispatch loop: validate each RTU frame, run it through
 * the bl_session state machine, reply, and reset the MCU after a COMMIT ack has
 * drained. Never returns under normal operation. */
void bl_update_mode(const iflash_backend_t *fl, bootcfg_t *cfg);

#endif
