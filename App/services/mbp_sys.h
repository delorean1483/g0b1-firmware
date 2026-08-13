#ifndef MBP_SYS_H
#define MBP_SYS_H
#include "types.h"
#define MB_RELAY_FW_VERSION 100u   /* reg 39: new STM32 relay-board firmware revision */

typedef void (*mb_reset_fn)(void); /* reg 34 write action; real NVIC_SystemReset deferred to bench */
void mbp_sys_register(mb_reset_fn on_reset);
#endif /* MBP_SYS_H */
