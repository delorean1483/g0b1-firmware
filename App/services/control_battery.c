#include "control.h"
#include "nvm.h"
#include "nvm_map.h"
#include "app_timers.h"

enum { BM_START = 0, BM_BATT_MONITOR, BM_START_ENGINE, BM_CHARGING,
       BM_BATT_STABLE_2MIN, BM_BATT_CHECK, BM_ERROR_PROCESS };

void control_battery_mode(apu_ctx_t *ctx) {
    switch (ctx->sub_state) {
        case BM_START:
            control_deenergize_all(ctx);   /* also sets engine_op_status/control_status = ST_OFF */
            ctx->attempted_start_counter = 0;
            ctx->attempted_charging_counter = 0;
            ctx->sub_state = BM_BATT_MONITOR;
            break;
        case BM_BATT_MONITOR:
            if (ctx->battery_voltage < ctx->batt_monitor_setting) {
                app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 1000);   /* 10 s confirm */
                ctx->sub_state = BM_START_ENGINE;
            } else {
                ctx->attempted_charging_counter = 0;
            }
            break;
        case BM_START_ENGINE:
            if (!app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                if (ctx->battery_voltage > ctx->batt_monitor_setting) {   /* recovered < 10 s */
                    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 0);
                    ctx->sub_state = BM_BATT_MONITOR;
                }
            } else {                                                     /* 10 s elapsed */
                ctx->attempted_charging_counter++;
                if (ctx->attempted_charging_counter > 3) {
                    ctx->attempted_charging_counter = 0;
                    ctx->sub_state = BM_ERROR_PROCESS;
                } else {
                    ctx->op_state_previous = OP_BATTERY;
                    ctx->op_state = OP_ENGINE_START;
                    ctx->sub_state = 0;                                  /* ES_GLOWPLUG_ON */
                    ctx->attempted_start_counter = 0;
                }
            }
            break;
        case BM_CHARGING:
            app_timer_set(SCALE_MINUTE, CHARGING_BATT_TMR, 30);
            ctx->control_status = ST_CHARGING;
            ctx->sub_state = BM_BATT_STABLE_2MIN;
            break;
        case BM_BATT_STABLE_2MIN:
            if (app_timer_expired(SCALE_MINUTE, CHARGING_BATT_TMR)) {
                ctx->out.fuel_pump = false;
                ctx->cool_mode = false;
                ctx->control_status = ST_OFF;
                app_timer_set(SCALE_SECOND, BATT_STABLE_TMR, 120);
                ctx->sub_state = BM_BATT_CHECK;
            }
            break;
        case BM_BATT_CHECK:
            if (app_timer_expired(SCALE_SECOND, BATT_STABLE_TMR)) {
                ctx->sub_state = BM_BATT_MONITOR;
            }
            break;
        case BM_ERROR_PROCESS:
            ctx->error_state = ERR_LOW_BATTERY;
            ctx->op_state = OP_ERROR_SHUTDOWN;
            break;
        default:
            break;
    }

    /* Monitor engine temperature, oil pressure, and standby every tick. */
    if (!ctx->engine_temp_ok) {
        ctx->error_state = ERR_HIGH_ENGINE_TEMP;
        ctx->op_state = OP_ERROR_SHUTDOWN;
    } else if (ctx->out.fuel_pump && !ctx->in_oil_pressure_ok) {
        ctx->error_state = ERR_LOW_OIL;
        ctx->op_state = OP_ERROR_SHUTDOWN;
    }
    if (!ctx->standby_override && ctx->control_status != ST_OFF && ctx->in_truck_ignition) {
        ctx->attempted_charging_counter = 0;
        ctx->op_state_previous = OP_BATTERY;
        ctx->error_state = ERR_STANDBY;
        ctx->op_state = OP_ERROR_SHUTDOWN;
    }
}

void control_battery_sample_settings(apu_ctx_t *ctx) {
    ctx->batt_monitor_setting = nvm_read_word(EE_MONITOR_BATT_SETTING);
}
