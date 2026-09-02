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
 * pulley ratio of the HP2000 APU. The port started from the PIC's integer 6
 * (rpm_engine = freq*6); bench measurement of the real tach against a reference
 * RPM (OI-4, 2026-09-02) put the true ratio at 6.8125 (= 109/16, i.e. ~8.81
 * pulses/rev). Kept as a rational so the arithmetic stays integer-exact:
 *   num = 109 * 500000 / 16 = 3,406,250 (exact at the driver's 500 kHz). */
#define RPM_FREQ_MULT_NUM   109u
#define RPM_FREQ_MULT_DEN   16u

uint16_t rpm_from_period_ticks(uint32_t period_ticks, uint32_t timer_hz);

#endif /* RPM_CALC_H */
