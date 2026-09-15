#ifndef RPM_CALC_H
#define RPM_CALC_H
#include "types.h"

/* Tach period -> engine RPM (bring-up helper, host-tested).
 *
 * A timer input-capture measures the period between consecutive tach pulses as
 * a count of timer ticks. Pulse frequency = timer_hz / period_ticks, and engine
 * RPM = MULT * frequency, where MULT = RPM_FREQ_MULT_NUM / RPM_FREQ_MULT_DEN:
 *
 *     RPM = round( RPM_FREQ_MULT_NUM * timer_hz / (RPM_FREQ_MULT_DEN * period_ticks) )
 *
 * period_ticks == 0 returns 0 (guard); the result saturates at UINT16_MAX. */

/* Engine RPM per Hz of tach-pulse frequency. Bundles the alternator poles x
 * pulley ratio of the HP2000 APU. History: the port started from the PIC's
 * integer 6; a 2026-09-02 bench refit (OI-4) raised it to 6.8125 (109/16) — but
 * that was fit to a NOISY tach input (the TIM3 capture had no glitch filter), so
 * it over-corrected. After enabling the input-capture filter (ICFilter=12,
 * 2026-09-15) the reading stabilised (jitter 75->~30 RPM) and exposed a clean,
 * constant +9.4% bias vs a reference digital tach (30-sample avg 2688 dashboard
 * @ ~2453 true, TRUCK-001). Refit to 6.22 (= 311/50) which maps 2688 -> ~2454.
 * Kept as a rational so the arithmetic stays integer-exact:
 *   num = 311 * 500000 / 50 = 3,110,000 (exact at the driver's 500 kHz). */
#define RPM_FREQ_MULT_NUM   311u
#define RPM_FREQ_MULT_DEN   50u

uint16_t rpm_from_period_ticks(uint32_t period_ticks, uint32_t timer_hz);

#endif /* RPM_CALC_H */
