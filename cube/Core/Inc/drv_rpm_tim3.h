#ifndef DRV_RPM_TIM3_H
#define DRV_RPM_TIM3_H

/* Concrete TIM3_CH1 tach input-capture backend for the portable rpm service
 * (Task 9, OI-4).
 *
 * PA6 = TIM3_CH1 (AF1); TIM3 free-runs at 500 kHz and each rising tach edge
 * yields a period that converts to RPM via the host-tested rpm_from_period_ticks.
 * drv_rpm_source() plugs into mbp_sensors_register(rpm_src) (reg 38) and any
 * control RPM consumer.
 *
 * Bench bring-up plan Task 9:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "rpm.h"

void drv_rpm_tim3_init(void);          /* start TIM3_CH1 input-capture (interrupt) */
const rpm_source_t *drv_rpm_source(void);  /* factory: hand to mbp_sensors_register */

#endif /* DRV_RPM_TIM3_H */
