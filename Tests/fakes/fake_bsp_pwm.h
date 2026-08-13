#ifndef FAKE_BSP_PWM_H
#define FAKE_BSP_PWM_H
#include "bsp_pwm_backend.h"
#include "board_pins.h"
void     fake_bsp_pwm_init(bsp_pwm_backend_t *be);   /* wire fake; all duties 0 */
uint16_t fake_bsp_pwm_duty(bsp_pwm_ch_t ch);
#endif
