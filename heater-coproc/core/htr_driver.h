#ifndef HTR_DRIVER_H
#define HTR_DRIVER_H
/* One heater protocol = one htr_driver_t. Adding a heater means adding a
   driver and one line in the registry passed to htr_fsm_init(); the FSM,
   register map and G0B1 side never change. */
#include "htr_types.h"
#include "htr_uart.h"

typedef struct htr_driver {
    uint8_t     type_id;   /* HTR_TYPE_* */
    uint8_t     wiring;    /* HTR_WIRING_3W / HTR_WIRING_4W */
    uint16_t    caps;      /* HTR_CAP_* */
    void       *ctx;
    /* Reset all protocol state and bind the UART. Sends nothing. */
    void (*init)(void *ctx, const htr_uart_t *u, uint32_t now);
    /* Non-blocking service: RX parse + scheduled TX. */
    void (*process)(void *ctx, uint32_t now);
    void (*start)(void *ctx, const htr_request_t *rq);
    /* Request a stop; the protocol keeps talking through cooldown. Never cuts power. */
    void (*stop)(void *ctx);
    /* Live level / setpoint / ambient update. */
    void (*apply)(void *ctx, const htr_request_t *rq);
    /* One auto-detect step (call repeatedly after init). May only transmit
       requests that cannot start a heater or change its settings. */
    htr_probe_t (*probe)(void *ctx, uint32_t now);
    void (*status)(const void *ctx, uint32_t now, htr_status_t *out);
} htr_driver_t;

#endif /* HTR_DRIVER_H */
