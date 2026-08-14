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
        default:
            break;
    }
}

void control_battery_sample_settings(apu_ctx_t *ctx) {
    ctx->batt_monitor_setting = nvm_read_word(EE_MONITOR_BATT_SETTING);
}
