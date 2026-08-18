#ifndef DRV_BSP_ADC_H
#define DRV_BSP_ADC_H

/* Concrete ADC1 + circular-DMA feed for the portable sensors service (Task 8).
 *
 * Free-running scan of the three consumed channels — IN0 (PA0, enclosure),
 * IN3 (PA3, battery), IN4 (PA4, external NTC) — into a live DMA snapshot.
 * drv_bsp_adc_drain() pushes that snapshot into sensors_add_sample() and is
 * meant to run on a scheduler slot (SLOT_100MS), not an ISR.
 *
 * A/C-pressure (IN1/IN2) and engine-coolant (IN5) channels are M3-deferred
 * (no conversions/registers yet) and intentionally not scanned here.
 *
 * Bench bring-up plan Task 8:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "types.h"

void     drv_bsp_adc_init(void);   /* calibrate + start continuous scan + circular DMA */
void     drv_bsp_adc_drain(void);  /* feed latest counts to sensors (call from a slot) */
uint16_t drv_bsp_adc_raw(uint8_t idx);  /* bench aid: raw counts 0=encl,1=batt,2=ext */

#endif /* DRV_BSP_ADC_H */
