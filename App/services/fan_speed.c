#include "fan_speed.h"

/* percent 0..100 -> PWM duty permille:
 *   0        -> 0        (off)
 *   1..100   -> FAN_MIN_DUTY .. 1000, linear (pct==1 -> floor, pct==100 -> full)
 * Values above 100 are clamped to full (fail-safe: never under-drive on bad input). */
uint16_t fan_duty_permille(uint8_t pct) {
    if (pct == 0u) return 0u;
    if (pct >= FAN_MAX_PCT) return 1000u;
    return (uint16_t)(FAN_MIN_DUTY +
        ((uint32_t)(pct - 1u) * (1000u - FAN_MIN_DUTY)) / (FAN_MAX_PCT - 1u));
}
