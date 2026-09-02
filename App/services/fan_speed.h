#ifndef FAN_SPEED_H
#define FAN_SPEED_H
#include "types.h"
/* Evap-fan speed is a percent 0..100 (0 = off). It maps to PWM duty (permille):
 *   0      -> 0                       (off)
 *   1..100 -> [FAN_MIN_DUTY .. 1000]  (linear; the fan will not physically spin
 *                                      below the minimum-spin floor)
 * FAN_MIN_DUTY defaults to 318 permille (32 %, = the PIC's old LOW = 7/22 ms) and
 * is a single bench-tunable constant. */
#define FAN_MIN_DUTY 318u   /* permille; lowest duty at which the fan spins (bench-tunable) */
#define FAN_MAX_PCT  100u
uint16_t fan_duty_permille(uint8_t pct);   /* percent 0..100 -> permille 0..1000 */

/* AUTO cooling fan law: e = clamp(cabin_f - setpoint_f, 0, SPAN);
 * pct = MIN + (100 - MIN) * e / SPAN. At/below target -> MIN; >= SPAN over -> 100. */
#define AUTO_FAN_MIN_PCT 32u   /* auto-ramp floor (percent), tunable */
#define AUTO_FAN_SPAN_F   6    /* degF of cooling gap that maps to 100% */
uint8_t fan_auto_pct(int16_t cabin_f, int16_t setpoint_f);   /* AUTO cooling fan percent */
#endif /* FAN_SPEED_H */
