/* drv_rpm_tim3.c — TIM3_CH1 tach input-capture implementation of rpm_source_t
 * (Task 9, OI-4).
 *
 * PA6 = TIM3_CH1 (AF1) carries the BJT-buffered tach pulse. TIM3 free-runs at
 * 500 kHz (64 MHz / 128, 16-bit, ARR=0xFFFF); each rising edge captures CCR1,
 * and the period between consecutive edges (ticks) converts to RPM via the
 * host-tested rpm_from_period_ticks(). 500 kHz keeps good resolution (3000
 * ticks at 1000 RPM) while the 16-bit counter still spans down to ~46 RPM
 * before wrapping — well below any running/cranking point; below that the
 * capture-timeout reports 0 (engine stopped).
 *
 * All register/BCD/threshold semantics stay in the portable rpm service; this
 * driver only produces a smoothed RPM behind the rpm_source_t seam.
 *
 * Bench bring-up plan Task 9:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "drv_rpm_tim3.h"
#include "rpm.h"          /* rpm_source_t */
#include "rpm_calc.h"     /* rpm_from_period_ticks (host-tested) */
#include "main.h"         /* CubeMX HAL: htim3, HAL_TIM_* */
#include <stdbool.h>

extern TIM_HandleTypeDef htim3;   /* CubeMX-generated (MX_TIM3_Init) */

#define DRV_RPM_TIMER_HZ    500000u   /* TIM3 tick rate: 64 MHz / (PSC 127 + 1) */
#define DRV_RPM_TIMEOUT_MS  1000u     /* no edge for 1 s => engine stopped (PIC RPM_STOP_TMR) */

static volatile uint16_t s_last_ccr;      /* previous capture value */
static volatile uint16_t s_period;        /* smoothed period between edges (ticks) */
static volatile uint32_t s_last_edge_ms;  /* HAL_GetTick at the last edge */
static volatile uint8_t  s_have_prev;     /* a previous CCR is on record */
static volatile uint8_t  s_seeded;        /* s_period holds a valid measurement */

void drv_rpm_tim3_init(void)
{
    s_have_prev = 0u;
    s_seeded    = 0u;
    HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
}

/* Overrides the weak HAL callback; fired from the generated TIM3_IRQHandler. */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3) return;

    uint16_t ccr = (uint16_t)HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);
    uint32_t now = HAL_GetTick();
    bool stale = (s_have_prev && (uint32_t)(now - s_last_edge_ms) > DRV_RPM_TIMEOUT_MS);
    s_last_edge_ms = now;

    if (!s_have_prev || stale) {          /* first edge, or restart after a gap */
        s_last_ccr  = ccr;
        s_have_prev = 1u;
        s_seeded    = 0u;                  /* re-seed the period on the next edge */
        return;
    }

    uint16_t d = (uint16_t)(ccr - s_last_ccr);   /* 16-bit wrap-safe */
    s_last_ccr = ccr;
    if (d == 0u) return;                          /* ignore degenerate double-capture */

    if (!s_seeded) { s_period = d; s_seeded = 1u; }
    else s_period = (uint16_t)(((uint32_t)s_period * 3u + d + 2u) >> 2);  /* EMA /4 */
}

static uint16_t drv_get_rpm(void *ctx)
{
    (void)ctx;
    if (!s_seeded) return 0u;
    if ((uint32_t)(HAL_GetTick() - s_last_edge_ms) > DRV_RPM_TIMEOUT_MS) return 0u;
    return rpm_from_period_ticks(s_period, DRV_RPM_TIMER_HZ);
}

static const rpm_source_t s_src = { .get_rpm = drv_get_rpm, .ctx = NULL };

const rpm_source_t *drv_rpm_source(void) { return &s_src; }
