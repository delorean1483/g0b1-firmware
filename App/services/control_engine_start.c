#include "control.h"
#include "app_timers.h"
#include "sensors.h"   /* SENSOR_OFF */

enum { ES_GLOWPLUG_ON = 0, ES_HEAT_ON, ES_FUEL_ON, ES_STARTER_ON,
       ES_ENGINE_ON, ES_CHECK_PRESSURE, ES_COOL_ON };

static void es_set_glow_duration(apu_ctx_t *ctx) {
    if (ctx->ext_temp_sensor_state == SENSOR_OFF) {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 280);   /* 28 s */
    } else if (ctx->external_temperature >= 122) {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 0);
        ctx->out.glow_plug = false;
    } else if (ctx->external_temperature >= 104) {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 80);    /* 8 s */
    } else if (ctx->external_temperature >= 68) {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 100);   /* 10 s */
    } else if (ctx->external_temperature >= 32) {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 160);   /* 16 s */
    } else {
        app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 290);   /* 29 s */
    }
}

void control_engine_start_mode(apu_ctx_t *ctx) {
    switch (ctx->sub_state) {
        case ES_GLOWPLUG_ON:
            ctx->out.glow_plug = true;
            ctx->control_status = ST_WARMING_UP;
            es_set_glow_duration(ctx);
            ctx->attempted_start_counter++;
            ctx->sub_state = ES_FUEL_ON;
            break;
        case ES_FUEL_ON:
            if (app_timer_expired(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR)) {
                ctx->out.glow_plug = false;
                if (app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                    ctx->out.fuel_pump = true;
                    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 100);   /* 1 s */
                    ctx->sub_state = ES_STARTER_ON;
                }
            }
            break;
        case ES_STARTER_ON:
            if (app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                ctx->out.starter = true;
                ctx->control_status = ST_STARTING;
                app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 400);       /* 4 s */
                ctx->sub_state = ES_ENGINE_ON;
            }
            break;
        case ES_ENGINE_ON:
            if (app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                ctx->out.starter = false;
                if (ctx->external_temperature < 122) {
                    ctx->out.glow_plug = true;
                    app_timer_set(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR, 50); /* post-heat 5 s */
                }
                app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 1000);      /* 10 s */
                ctx->sub_state = ES_CHECK_PRESSURE;
            }
            break;
        case ES_CHECK_PRESSURE:
            if (app_timer_expired(SCALE_HUNDRED_MS, GLOW_PLUG_ON_TMR)) {
                ctx->out.glow_plug = false;
            }
            if (app_timer_expired(SCALE_TEN_MS, SHORT_DELAY_TMR)) {
                if (!ctx->in_oil_pressure_ok) {                 /* oil low (PIC NOK) */
                    if (ctx->attempted_start_counter >= 5) {
                        ctx->attempted_start_counter = 0;
                        ctx->error_state = ERR_STARTING_FAILURE;
                        ctx->op_state = OP_ERROR_SHUTDOWN;
                    } else {
                        ctx->out.fuel_pump = false;
                        ctx->sub_state = ES_GLOWPLUG_ON;        /* retry */
                    }
                } else {                                        /* oil OK */
                    app_timer_set(SCALE_TEN_MS, SHORT_DELAY_TMR, 0);
                    ctx->attempted_start_counter = 0;
                    ctx->engine_op_status = ST_RUNNING;
                    ctx->control_status = ST_RUNNING;
                    ctx->sub_state = ES_COOL_ON;
                }
            }
            break;
        default:
            break;
    }
}
