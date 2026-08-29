#ifndef DRV_BSP_PWM_H
#define DRV_BSP_PWM_H

/* Concrete STM32G0 TIM2 backend for the portable bsp_pwm service (App/services).
 *
 * TIM2 CH1 = PC4 (evap fan), CH2 = PC5 (condenser fan), ~45 Hz (PIC18 22 ms
 * period; a slow rate is required to keep the high-side PROFET switching loss low).
 * Duty is permille 0..1000 (bsp_pwm_ch_t: PWM_EVAP_FAN=0, PWM_CONDENSER_FAN=1).
 *
 * Bench bring-up plan Task 4:
 *   docs/superpowers/plans/2026-08-17-stm32g0-apu-bench-bringup.md
 */
#include "bsp_pwm_backend.h"

/* Factory: hand this to bsp_pwm_init(). */
const bsp_pwm_backend_t *drv_bsp_pwm_backend(void);

/* Start TIM2 CH1/CH2 PWM output at 0 % (fans OFF = safe default). Call once
 * after bsp_pwm_init(), before the scheduler. Requires the CubeMX-generated
 * MX_TIM2_Init() to have run (it does — generated main() calls it before app_main). */
void drv_bsp_pwm_start(void);

#endif /* DRV_BSP_PWM_H */
