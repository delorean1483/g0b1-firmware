#include "control.h"
#include "bsp_io.h"
#include "bsp_pwm.h"
#include "fan_speed.h"
#include "board_pins.h"

void outputs_apply(const apu_ctx_t *ctx) {
    const apu_outputs_t *o = &ctx->out;
    bsp_out_set(OUT_FUEL_PUMP,         o->fuel_pump);
    bsp_out_set(OUT_STARTER,           o->starter);
    bsp_out_set(OUT_GLOW_PLUG,         o->glow_plug);
    bsp_out_set(OUT_COMPRESSOR_CLUTCH, o->compressor_clutch);
    bsp_out_set(OUT_HEAT_REVERSER,     o->heat_reverse);   /* OI-1 */

    bsp_out_set(OUT_EVAP_FAN, o->evap_fan);
    bsp_pwm_set(PWM_EVAP_FAN, o->evap_fan ? fan_speed_permille(o->evap_speed) : 0u);

    bsp_out_set(OUT_CONDENSER_FAN, o->condenser_fan);
    bsp_pwm_set(PWM_CONDENSER_FAN, o->condenser_fan ? o->condenser_duty : 0u);
}
