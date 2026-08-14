#include "control.h"
#include "nvm.h"
#include "nvm_map.h"
#include "fan_speed.h"
#include "app_timers.h"

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

enum { CC_START_SETTLE = 0, CC_START_ENGINE, CC_MONITOR_TEMP, CC_START_COOL,
       CC_SWITCH_TO_COOL, CC_COMP_ON, CC_AC_LOW_PRESSURE_RECHK, CC_EVAP_ON,
       CC_CTRL_RUNNING, CC_HEAT_DEFROST, CC_COOL_DEFROST_END, CC_HEAT_SWITCHFROM_COOL,
       CC_EVAP_OFF, CC_AC_LOW_PRESSURE_FAIL, CC_AC_HIGH_PRESSURE_FAIL,
       CC_ANTI_STALL_STEP1, CC_ANTI_STALL_STEP2, CC_AC_HIGH_PRESSURE_RECHK,
       CC_WAIT_HIGH_PRESSURE_NORMAL };

#define CC_TEMP_OFFSET 3

void control_climate_mode(apu_ctx_t *ctx) {
    switch (ctx->sub_state) {
        case CC_START_SETTLE:
            ctx->temp_display_state = TD_REAL_TIME;
            app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 100);   /* 1 s settle */
            ctx->sub_state = CC_START_ENGINE;
            break;
        case CC_START_ENGINE:
            if (app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                if (!ctx->out.fuel_pump) {                       /* engine not running */
                    ctx->op_state_previous = OP_CLIMATE;
                    ctx->op_state = OP_ENGINE_START;
                    ctx->sub_state = 0;                          /* ES_GLOWPLUG_ON */
                    ctx->attempted_start_counter = 0;
                } else {
                    ctx->sub_state = CC_MONITOR_TEMP;
                }
            }
            break;
        case CC_MONITOR_TEMP:
            if (ctx->cabin_temperature <= (ctx->clmt_temp_setting - CC_TEMP_OFFSET)) {
                ctx->temp_display_state = TD_REAL_TIME;
                ctx->control_status = ST_CHILLIN;
            }
            if (ctx->cabin_temperature >= (ctx->clmt_temp_setting + CC_TEMP_OFFSET)) {
                ctx->temp_display_state = TD_REAL_TIME;
                ctx->control_status = ST_COOLING;
                ctx->sub_state = CC_START_COOL;
            }
            break;
        default:
            break;
    }
}
