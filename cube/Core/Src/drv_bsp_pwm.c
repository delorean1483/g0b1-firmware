/* drv_bsp_pwm.c — STM32G0 TIM2 implementation of bsp_pwm_backend_t (Task 4).
 *
 * Two fan-PWM channels for the EF-G0B1R:
 *   PWM_EVAP_FAN      -> TIM2_CH1 = PC4  (evap fan speed, permille 0..1000)
 *   PWM_CONDENSER_FAN -> TIM2_CH2 = PC5  (condenser fan, OI-2 head-pressure ramp)
 *
 * TIM2 runs at ~1 kHz: 64 MHz / (PSC+1=64) / (ARR+1=1000) = 1000 Hz. The compare
 * value is derived from the live ARR (htim2.Init.Period) so the mapping stays
 * correct if the .ioc period changes: CCR = permille * (ARR+1) / 1000, i.e.
 * permille==1000 -> CCR==ARR+1 (100 %), permille==0 -> CCR==0 (0 %).
 *
 * get() returns the last commanded permille (not re-derived from the CCR).
 */
#include "drv_bsp_pwm.h"
#include "bsp_pwm.h"   /* portable façade: BSP_PWM_MAX, bsp_pwm_ch_t, PWM_COUNT */
#include "main.h"      /* CubeMX HAL: TIM_HandleTypeDef, __HAL_TIM_*, HAL_TIM_PWM_Start */

extern TIM_HandleTypeDef htim2;   /* CubeMX-generated (MX_TIM2_Init) */

/* index == bsp_pwm_ch_t */
static const uint32_t s_tim_ch[PWM_COUNT] = {
    [PWM_EVAP_FAN]      = TIM_CHANNEL_1,   /* PC4 */
    [PWM_CONDENSER_FAN] = TIM_CHANNEL_2,   /* PC5 */
};

static uint16_t s_duty[PWM_COUNT];   /* last commanded permille, for get() */

static void pwm_set(void *ctx, uint8_t ch, uint16_t permille)
{
    (void)ctx;
    if (ch >= PWM_COUNT) return;
    if (permille > BSP_PWM_MAX) permille = BSP_PWM_MAX;   /* façade clamps too */
    s_duty[ch] = permille;
    uint32_t arr1 = htim2.Init.Period + 1u;               /* counts per PWM period */
    uint32_t ccr  = ((uint32_t)permille * arr1) / 1000u;  /* permille -> compare */
    __HAL_TIM_SET_COMPARE(&htim2, s_tim_ch[ch], ccr);
}

static uint16_t pwm_get(void *ctx, uint8_t ch)
{
    (void)ctx;
    return (ch < PWM_COUNT) ? s_duty[ch] : 0u;
}

static const bsp_pwm_backend_t s_backend = {
    .set = pwm_set,
    .get = pwm_get,
    .ctx = NULL,
};

const bsp_pwm_backend_t *drv_bsp_pwm_backend(void) { return &s_backend; }

void drv_bsp_pwm_start(void)
{
    for (int c = 0; c < (int)PWM_COUNT; ++c) {
        __HAL_TIM_SET_COMPARE(&htim2, s_tim_ch[c], 0u);   /* 0 % before enabling output */
        s_duty[c] = 0u;
        HAL_TIM_PWM_Start(&htim2, s_tim_ch[c]);
    }
}
