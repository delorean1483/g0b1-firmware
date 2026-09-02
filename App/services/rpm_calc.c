#include "rpm_calc.h"

uint16_t rpm_from_period_ticks(uint32_t period_ticks, uint32_t timer_hz)
{
    if (period_ticks == 0u) return 0u;                       /* guard divide-by-zero */
    /* num = MULT * timer_hz with MULT = 109/16.  109*500000 = 54,500,000 fits a
     * uint32 (max ~4.29e9); /16 is exact at 500 kHz -> 3,406,250. */
    uint32_t num = (RPM_FREQ_MULT_NUM * timer_hz) / RPM_FREQ_MULT_DEN;
    uint32_t rpm = (num + period_ticks / 2u) / period_ticks; /* freq*MULT, rounded */
    return (rpm > 65535u) ? 65535u : (uint16_t)rpm;          /* saturate, never wrap */
}
