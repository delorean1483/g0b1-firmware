#ifndef BSP_PWM_BACKEND_H
#define BSP_PWM_BACKEND_H
#include "types.h"
/* Abstract fan-PWM hardware (duty in permille 0..1000). TIM2 CH1/CH2 HAL deferred. */
typedef struct bsp_pwm_backend {
    void     (*set)(void *ctx, uint8_t ch, uint16_t permille);
    uint16_t (*get)(void *ctx, uint8_t ch);
    void *ctx;
} bsp_pwm_backend_t;
#endif /* BSP_PWM_BACKEND_H */
