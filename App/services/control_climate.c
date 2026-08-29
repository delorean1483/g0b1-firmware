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
    ctx->evap_fan_speed = (sp > 100u) ? 100u : sp;   /* percent 0..100 */
}

enum { CC_START_SETTLE = 0, CC_START_ENGINE, CC_MONITOR_TEMP, CC_START_COOL,
       CC_SWITCH_TO_COOL, CC_COMP_ON, CC_AC_LOW_PRESSURE_RECHK, CC_EVAP_ON,
       CC_CTRL_RUNNING, CC_HEAT_DEFROST, CC_COOL_DEFROST_END, CC_HEAT_SWITCHFROM_COOL,
       CC_EVAP_OFF, CC_AC_LOW_PRESSURE_FAIL, CC_AC_HIGH_PRESSURE_FAIL,
       CC_ANTI_STALL_STEP1, CC_ANTI_STALL_STEP2, CC_AC_HIGH_PRESSURE_RECHK,
       CC_WAIT_HIGH_PRESSURE_NORMAL };

#define CC_TEMP_OFFSET 3
#define CONDENSER_STUB_DUTY 1000u   /* OI-2: full airflow stub; head-pressure ramp deferred */

void control_climate_mode(apu_ctx_t *ctx) {
    /* Track the evap-fan speed live: control_climate_sample_settings refreshes
       evap_fan_speed from NVM every 1 s, so a HMI/Modbus speed change (reg 12)
       takes effect without re-entering cooling. (Was latched once at CC_EVAP_ON,
       which made runtime speed changes inert.) outputs_apply reads out.evap_speed. */
    ctx->out.evap_speed = ctx->evap_fan_speed;   /* percent 0..100 */
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
        case CC_START_COOL:
            ctx->cool_mode = true;
            ctx->out.heat_reverse = false;                  /* cool = PB4 de-energized (OI-1) */
            ctx->sub_state = CC_COMP_ON;
            break;
        case CC_COMP_ON:
            if (ctx->compressor_off_timer >= 15u) {
                ctx->out.compressor_clutch = true;
                app_timer_set(SCALE_SECOND, COMP_EVAP_DELAY_TMR, 0);
                ctx->sub_state = CC_EVAP_ON;
            }
            break;
        case CC_EVAP_ON:
            if (app_timer_expired(SCALE_SECOND, COMP_EVAP_DELAY_TMR)) {
                ctx->out.evap_fan = true;
                app_timer_set(SCALE_SECOND, EVAP_FORCED_ON_TMR, 10);
                app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 30);
                ctx->sub_state = CC_CTRL_RUNNING;
            }
            break;
        case CC_CTRL_RUNNING:
            if (app_timer_get(SCALE_MINUTE, DEFROST_CYCLE_TMR) > 0u) {
                if (ctx->cool_mode &&
                    ctx->cabin_temperature <= (ctx->clmt_temp_setting + 1)) {
                    ctx->out.compressor_clutch = false;
                    ctx->temp_display_state = TD_CC_SETTING;
                    ctx->sub_state = CC_EVAP_OFF;
                }
            } else {                                        /* defrost cycle */
                ctx->control_status = ST_DEFROST;
                ctx->temp_display_state = TD_REAL_TIME;
                if (ctx->cool_mode) {
                    ctx->out.compressor_clutch = false;
                    ctx->cool_mode = false;
                    ctx->out.evap_fan = false;
                    app_timer_set(SCALE_SECOND, EVENT_INTERVAL_TMR, 45);
                    ctx->sub_state = CC_COOL_DEFROST_END;
                }
            }
            break;
        case CC_EVAP_OFF:
            if (ctx->compressor_off_timer >= 15u) {
                ctx->cool_mode = false;
                ctx->out.evap_fan = false;
                ctx->sub_state = CC_MONITOR_TEMP;
            } else if (ctx->cabin_temperature <= (ctx->clmt_temp_setting - CC_TEMP_OFFSET)) {
                ctx->temp_display_state = TD_REAL_TIME;
                ctx->cool_mode = false;
            }
            break;
        case CC_COOL_DEFROST_END:
            if (app_timer_expired(SCALE_SECOND, EVENT_INTERVAL_TMR)) {
                ctx->out.compressor_clutch = true;
                ctx->cool_mode = true;
                ctx->out.evap_fan = true;
                ctx->control_status = ST_COOLING;
                app_timer_set(SCALE_MINUTE, DEFROST_CYCLE_TMR, 30);
                ctx->sub_state = CC_CTRL_RUNNING;
            }
            break;
        case CC_AC_LOW_PRESSURE_RECHK:
            ctx->out.compressor_clutch = false;
            ctx->refregerant_check_counter++;
            if (ctx->refregerant_check_counter > 10u) {
                ctx->refregerant_check_counter = 0;
                ctx->sub_state = CC_AC_LOW_PRESSURE_FAIL;
            } else {
                ctx->sub_state = CC_COMP_ON;
            }
            break;
        case CC_AC_LOW_PRESSURE_FAIL:
            ctx->op_state_previous = OP_CLIMATE;
            ctx->error_state = ERR_AC_LOW_PRESSURE;
            ctx->op_state = OP_ERROR_SHUTDOWN;
            break;
        case CC_AC_HIGH_PRESSURE_RECHK:
            ctx->out.compressor_clutch = false;
            ctx->sub_state = CC_WAIT_HIGH_PRESSURE_NORMAL;
            break;
        case CC_WAIT_HIGH_PRESSURE_NORMAL:
            if (ctx->ac_high_pressure_ok) {
                ctx->out.compressor_clutch = true;
                app_timer_set(SCALE_SECOND, COMP_EVAP_DELAY_TMR, 0);
                ctx->sub_state = CC_EVAP_ON;
            }
            break;
        case CC_AC_HIGH_PRESSURE_FAIL:
            ctx->op_state_previous = OP_CLIMATE;
            ctx->error_state = ERR_AC_HIGH_PRESSURE;
            ctx->op_state = OP_ERROR_SHUTDOWN;
            break;
        default:
            break;
    }
    /* A/C pressure monitor (armed once compressor has run >= 2 s). */
    if (ctx->out.compressor_clutch && ctx->compressor_on_timer >= 2u) {
        if (!ctx->ac_low_pressure_ok) {
            ctx->out.compressor_clutch = false;
            ctx->sub_state = CC_AC_LOW_PRESSURE_RECHK;
        } else if (!ctx->ac_high_pressure_ok) {
            ctx->out.compressor_clutch = false;
            ctx->sub_state = CC_AC_HIGH_PRESSURE_RECHK;
        } else {
            ctx->refregerant_check_counter = 0;
        }
    }
    /* Engine over-temp shutdown. */
    if (!ctx->engine_temp_ok) {
        ctx->error_state = ERR_HIGH_ENGINE_TEMP;
        ctx->op_state = OP_ERROR_SHUTDOWN;
    }
    /* OI-2 condenser: follow the compressor at a fixed stub duty (ramp curve deferred). */
    ctx->out.condenser_fan = ctx->out.compressor_clutch;
    ctx->out.condenser_duty = ctx->out.compressor_clutch ? CONDENSER_STUB_DUTY : 0u;
}
