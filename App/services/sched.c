#include "sched.h"
#include "app_timers.h"

static uint32_t s_ms;                       /* monotonic millisecond counter */
static bool     s_due[SLOT_COUNT];
static sched_handler_fn s_handler[SLOT_COUNT];

void sched_init(void) {
    s_ms = 0u;
    for (uint8_t i = 0; i < SLOT_COUNT; i++) { s_due[i] = false; s_handler[i] = 0; }
    app_timers_init();
}

void sched_register(sched_slot_t slot, sched_handler_fn h) {
    if (slot < SLOT_COUNT) s_handler[slot] = h;
}

void sched_service(uint16_t elapsed_ms) {
    for (uint16_t k = 0; k < elapsed_ms; k++) {
        s_ms++;
        app_timers_tick(SCALE_MS);                             /* every 1 ms */
        if (s_ms % 10u   == 0u) { s_due[SLOT_10MS]  = true; app_timers_tick(SCALE_TEN_MS); }
        if (s_ms % 50u   == 0u) { s_due[SLOT_50MS]  = true; }
        if (s_ms % 100u  == 0u) { s_due[SLOT_100MS] = true; app_timers_tick(SCALE_HUNDRED_MS); }
        if (s_ms % 1000u == 0u) { s_due[SLOT_1S]    = true; app_timers_tick(SCALE_SECOND); }
        if (s_ms % 5000u == 0u) { s_due[SLOT_5S]    = true; }
        if (s_ms % 60000u== 0u) { s_due[SLOT_1MIN]  = true; app_timers_tick(SCALE_MINUTE); }
    }
}

void sched_run(void) {
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        if (s_due[i]) {
            if (s_handler[i]) s_handler[i]();
            s_due[i] = false;
        }
    }
}

bool sched_slot_due(sched_slot_t slot) { return (slot < SLOT_COUNT) ? s_due[slot] : false; }
