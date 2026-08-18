#include "rpm_calc.h"

uint16_t rpm_from_period_ticks(uint32_t period_ticks, uint32_t timer_hz)
{
    if (period_ticks == 0u) return 0u;                       /* guard divide-by-zero */
    uint32_t num = RPM_FREQ_MULT * timer_hz;                 /* 6 * 500000 = 3,000,000 */
    uint32_t rpm = (num + period_ticks / 2u) / period_ticks; /* freq*MULT, rounded */
    return (rpm > 65535u) ? 65535u : (uint16_t)rpm;          /* saturate, never wrap */
}
