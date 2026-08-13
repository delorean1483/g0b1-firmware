#include "bsp_pwm.h"

static const bsp_pwm_backend_t *s_be;

void bsp_pwm_init(const bsp_pwm_backend_t *be) { s_be = be; }
void bsp_pwm_set(bsp_pwm_ch_t ch, uint16_t permille) {
    if (permille > BSP_PWM_MAX) permille = BSP_PWM_MAX;
    if (s_be && s_be->set) s_be->set(s_be->ctx, (uint8_t)ch, permille);
}
uint16_t bsp_pwm_get(bsp_pwm_ch_t ch) { return (s_be && s_be->get) ? s_be->get(s_be->ctx, (uint8_t)ch) : 0u; }
