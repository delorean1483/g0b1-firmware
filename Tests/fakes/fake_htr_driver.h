#ifndef FAKE_HTR_DRIVER_H
#define FAKE_HTR_DRIVER_H
/* Scriptable heater drivers for FSM tests. fake_drv[0]: type 1, 3-wire;
   fake_drv[1]: type 2, 4-wire; fake_drv[2]: type 3, 3-wire. */
#include "htr_driver.h"

typedef struct {
    const htr_uart_t *uart;
    htr_probe_t probe_answer;    /* returned once probe_calls >= probe_after */
    unsigned probe_after;
    unsigned probe_calls, inits, starts, stops, applies, processes;
    bool alive;                  /* process()/probe() refresh last_rx when true */
    uint8_t hint;
    uint32_t last_rx;
    bool any_rx;
    htr_request_t last_rq;
} fake_drv_ctx_t;

extern fake_drv_ctx_t fake_ctx[3];
extern const htr_driver_t fake_drv[3];
void fake_drivers_reset(void);
#endif
