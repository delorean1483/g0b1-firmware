#ifndef FAKE_HTR_GPIO_H
#define FAKE_HTR_GPIO_H
/* Records every htr_gpio_t call as a compact event log; RX levels are
   programmable per wiring (constant or a duty pattern). */
#include "htr_iface.h"
#include <stddef.h>

typedef enum { EV_LINE = 1, EV_ATTACH = 2 } fake_ev_kind_t;
typedef struct { uint8_t kind, a, b; } fake_ev_t;   /* LINE: a=line b=active; ATTACH: a=wiring b=attach */

#define FAKE_GPIO_MAX_EV 256u
typedef struct {
    htr_gpio_t api;
    fake_ev_t ev[FAKE_GPIO_MAX_EV];
    size_t n;
    bool line[HTR_LINE_COUNT];
    bool attached[3];
    uint8_t high_pct[3];          /* per wiring: % of samples that read high */
    uint32_t reads[3];
} fake_gpio_t;

void fake_gpio_init(fake_gpio_t *g);
void fake_gpio_clear_log(fake_gpio_t *g);
#endif
