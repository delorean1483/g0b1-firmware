/* drv_bsp_adc.c — ADC1 + circular-DMA sensor feed (Task 8).
 *
 * Free-running, non-blocking (spec 5.4): ADC1 continuously scans the three
 * analog channels the portable sensors service consumes and a circular DMA
 * keeps a live snapshot in s_dma[]. drv_bsp_adc_drain() copies that snapshot
 * into sensors_add_sample() from a scheduler slot (SLOT_100MS) — never from an
 * ISR, so the conversion order stays decoupled from the control loop.
 *
 * Channel map (schematic G0B1 APU Manager R1.pdf p.3 / spec 5.4, cross-checked
 * vs sensors_cal.h): IN0 = PA0 enclosure (TMP6131), IN3 = PA3 battery divider,
 * IN4 = PA4 external NTC. With forward-scan (ascending channel number) the DMA
 * buffer lands in the fixed order [IN0, IN3, IN4] = [encl, batt, ext].
 *
 * Bench bring-up plan Task 8:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "drv_bsp_adc.h"
#include "sensors.h"
#include "main.h"      /* CubeMX HAL: hadc1, HAL_ADC_*, HAL_ADCEx_* */

extern ADC_HandleTypeDef hadc1;   /* CubeMX-generated (MX_ADC1_Init) */

/* Forward-scan order of the enabled regular channels: IN0 < IN3 < IN4. */
enum { ADC_IDX_ENCL = 0, ADC_IDX_BATT, ADC_IDX_EXT, ADC_NCH };

/* DMA target. volatile: written by DMA behind the CPU's back. uint16_t matches
 * the 12-bit right-aligned, halfword-DMA conversions; a 16-bit load is atomic
 * on the M0+, so drain() never sees a torn count. */
static volatile uint16_t s_dma[ADC_NCH];

void drv_bsp_adc_init(void)
{
    /* Self-calibrate (mandatory on G0 for rated accuracy) then start the
     * continuous scan + circular DMA. One start; the buffer self-refreshes. */
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_dma, ADC_NCH);
}

void drv_bsp_adc_drain(void)
{
    /* Feed the latest free-running counts to the portable rolling averager.
     * Reads may straddle conversion cycles — harmless for the slow average. */
    sensors_add_sample(SENS_ENCL, s_dma[ADC_IDX_ENCL]);
    sensors_add_sample(SENS_BATT, s_dma[ADC_IDX_BATT]);
    sensors_add_sample(SENS_EXT,  s_dma[ADC_IDX_EXT]);
}

uint16_t drv_bsp_adc_raw(uint8_t idx)
{
    return (idx < ADC_NCH) ? s_dma[idx] : 0u;
}
