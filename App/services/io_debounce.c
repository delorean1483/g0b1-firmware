#include "io_debounce.h"

void io_debounce_init(discrete_input_t *d, uint16_t debounce_time, uint8_t initial) {
    d->previous_state  = initial;
    d->debounced_state = initial;
    d->service_needed  = 0u;
    d->debounce_tmr    = 0u;
    d->debounce_time   = debounce_time;
}

void io_debounce_service(discrete_input_t *d, uint8_t raw) {
    if (raw != d->previous_state) {
        d->previous_state = raw;
        d->debounce_tmr = 1u;                 /* first sample of a new stable run */
    } else if (d->debounce_tmr < d->debounce_time) {
        d->debounce_tmr++;
    }
    if (d->debounce_tmr >= d->debounce_time && d->debounced_state != d->previous_state) {
        d->debounced_state = d->previous_state;
        d->service_needed = 1u;
    }
}

uint8_t io_debounce_state(const discrete_input_t *d) { return d->debounced_state; }
