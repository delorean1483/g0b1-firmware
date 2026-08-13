#include "app_timers.h"

static const uint8_t s_count[SCALE_COUNT] = {
    NUM_ONE_MS_TIMER, NUM_TEN_MS_TIMER, NUM_100_MS_TIMER, NUM_ONE_SECOND_TIMER, NUM_ONE_MINUTE_TIMER
};
static uint16_t s_ms[NUM_ONE_MS_TIMER];
static uint16_t s_ten[NUM_TEN_MS_TIMER];
static uint16_t s_hun[NUM_100_MS_TIMER];
static uint16_t s_sec[NUM_ONE_SECOND_TIMER];
static uint16_t s_min[NUM_ONE_MINUTE_TIMER];
static uint16_t *const s_arr[SCALE_COUNT] = { s_ms, s_ten, s_hun, s_sec, s_min };

void app_timers_init(void) {
    for (uint8_t sc = 0; sc < SCALE_COUNT; sc++)
        for (uint8_t i = 0; i < s_count[sc]; i++) s_arr[sc][i] = 0u;
}
void app_timer_set(app_timer_scale_t s, uint8_t idx, uint16_t ticks) {
    if (s < SCALE_COUNT && idx < s_count[s]) s_arr[s][idx] = ticks;
}
uint16_t app_timer_get(app_timer_scale_t s, uint8_t idx) {
    return (s < SCALE_COUNT && idx < s_count[s]) ? s_arr[s][idx] : 0u;
}
bool app_timer_expired(app_timer_scale_t s, uint8_t idx) { return app_timer_get(s, idx) == 0u; }
void app_timers_tick(app_timer_scale_t s) {
    if (s >= SCALE_COUNT) return;
    for (uint8_t i = 0; i < s_count[s]; i++) if (s_arr[s][i] > 0u) s_arr[s][i]--;
}
