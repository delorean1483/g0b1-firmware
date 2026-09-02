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

/* AUTO cooling fan percent: ramps from AUTO_FAN_MIN_PCT (at/below target) to
 * 100% (at/above AUTO_FAN_SPAN_F over target), linear in between. */
uint8_t fan_auto_pct(int16_t cabin_f, int16_t setpoint_f)
{
    int32_t e = (int32_t)cabin_f - (int32_t)setpoint_f;   /* cooling error, degF */
    if (e <= 0) return (uint8_t)AUTO_FAN_MIN_PCT;
    if (e >= AUTO_FAN_SPAN_F) return 100u;
    return (uint8_t)(AUTO_FAN_MIN_PCT + ((100u - AUTO_FAN_MIN_PCT) * (uint32_t)e) / (uint32_t)AUTO_FAN_SPAN_F);
}
