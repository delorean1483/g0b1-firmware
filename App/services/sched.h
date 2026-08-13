#ifndef SCHED_H
#define SCHED_H
#include "types.h"

typedef enum { SLOT_10MS = 0, SLOT_50MS, SLOT_100MS, SLOT_1S, SLOT_5S, SLOT_1MIN, SLOT_COUNT } sched_slot_t;
typedef void (*sched_handler_fn)(void);

void sched_init(void);                                  /* clears state + app_timers_init() */
void sched_register(sched_slot_t slot, sched_handler_fn h);
void sched_service(uint16_t elapsed_ms);                /* advance the clock */
void sched_run(void);                                   /* dispatch due slots to handlers */
bool sched_slot_due(sched_slot_t slot);                 /* test hook */

#endif /* SCHED_H */
