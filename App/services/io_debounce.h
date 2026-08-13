#ifndef IO_DEBOUNCE_H
#define IO_DEBOUNCE_H
#include "types.h"

#define DEBOUNCE_TIME 10u   /* consecutive 10 ms-slot samples => 100 ms */
#define SWITCH_OPEN   0u
#define SWITCH_CLOSED 1u

typedef struct {
    uint8_t  previous_state;   /* last raw sample (internal) */
    uint8_t  debounced_state;  /* committed state read by the app */
    uint8_t  service_needed;   /* set on a state change; app clears */
    uint16_t debounce_tmr;     /* consecutive-identical-sample count (internal) */
    uint16_t debounce_time;    /* samples required to commit */
} discrete_input_t;

void    io_debounce_init(discrete_input_t *d, uint16_t debounce_time, uint8_t initial);
void    io_debounce_service(discrete_input_t *d, uint8_t raw);
uint8_t io_debounce_state(const discrete_input_t *d);

#endif /* IO_DEBOUNCE_H */
