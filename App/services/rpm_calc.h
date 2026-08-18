#ifndef RPM_CALC_H
#define RPM_CALC_H
#include "types.h"

/* Tach period -> engine RPM (bring-up helper, host-tested).
 *
 * A timer input-capture measures the period between consecutive tach pulses as
 * a count of timer ticks. Pulse frequency = timer_hz / period_ticks, and engine
 * RPM = RPM_FREQ_MULT * frequency (the PIC ReadEngineRPM used rpm = freq * 6):
 *
 *     RPM = round( RPM_FREQ_MULT * timer_hz / period_ticks )
 *
 * period_ticks == 0 returns 0 (guard); the result saturates at UINT16_MAX. */

/* Engine RPM per Hz of tach-pulse frequency. Bundles the alternator poles x
 * pulley ratio of the HP2000 APU (PIC main.c ReadEngineRPM: rpm_engine = freq*6).
 * Engine-specific; bench-tunable when the real tach is characterised (OI-4). */
#define RPM_FREQ_MULT   6u

uint16_t rpm_from_period_ticks(uint32_t period_ticks, uint32_t timer_hz);

#endif /* RPM_CALC_H */
