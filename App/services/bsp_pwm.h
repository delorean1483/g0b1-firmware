#ifndef BSP_PWM_H
#define BSP_PWM_H
#include "types.h"
#include "board_pins.h"
#include "bsp_pwm_backend.h"
#define BSP_PWM_MAX 1000u
void     bsp_pwm_init(const bsp_pwm_backend_t *be);
void     bsp_pwm_set(bsp_pwm_ch_t ch, uint16_t permille);  /* clamps to BSP_PWM_MAX */
uint16_t bsp_pwm_get(bsp_pwm_ch_t ch);
#endif /* BSP_PWM_H */
