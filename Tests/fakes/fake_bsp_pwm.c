#include "fake_bsp_pwm.h"

static uint16_t s_duty[PWM_COUNT];

static void fp_set(void *ctx, uint8_t ch, uint16_t permille) { (void)ctx; if (ch < PWM_COUNT) s_duty[ch] = permille; }
static uint16_t fp_get(void *ctx, uint8_t ch) { (void)ctx; return (ch < PWM_COUNT) ? s_duty[ch] : 0u; }

void fake_bsp_pwm_init(bsp_pwm_backend_t *be) {
    for (int i = 0; i < PWM_COUNT; i++) s_duty[i] = 0u;
    be->set = fp_set; be->get = fp_get; be->ctx = 0;
}
uint16_t fake_bsp_pwm_duty(bsp_pwm_ch_t ch) { return (ch < PWM_COUNT) ? s_duty[ch] : 0u; }
