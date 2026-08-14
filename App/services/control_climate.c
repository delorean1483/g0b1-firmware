#include "control.h"
#include "sensors.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fan_speed.h"

void control_service_compressor_timers(apu_ctx_t *ctx) {
    if (!ctx->out.compressor_clutch) {
        ctx->compressor_on_timer = 0;
        if (ctx->compressor_off_timer < 255u) ctx->compressor_off_timer++;
    } else {
        ctx->compressor_off_timer = 0;
        if (ctx->compressor_on_timer < 255u) ctx->compressor_on_timer++;
    }
}

void control_climate_sample_settings(apu_ctx_t *ctx) {
    ctx->clmt_temp_setting = (int16_t)nvm_read_word(EE_CLIMATE_TEMP_SETTING);
    uint8_t sp = nvm_read_byte(EE_EVAP_FAN_SPEED);
    ctx->evap_fan_speed = (sp > (uint8_t)FAN_HIGH) ? (uint8_t)FAN_HIGH : sp;
}
