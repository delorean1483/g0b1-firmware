#include "control.h"

static control_mode_fn s_mode[OP_STATE_COUNT];
static uint8_t s_mode_prev;   /* last applied mode_request, for change detection */

void control_init(apu_ctx_t *ctx) {
    for (uint8_t i = 0; i < OP_STATE_COUNT; i++) s_mode[i] = 0;
    s_mode_prev = MODE_OFF;
    ctx->op_state = OP_POWER_UP;
    ctx->sub_state = 0;
    ctx->out = (apu_outputs_t){0};
    ctx->engine_op_status = ST_OFF;
    ctx->control_status = ST_OFF;
    ctx->error_state = ERR_NONE;
    ctx->oil_change_state = OIL_GOOD;
    ctx->temp_display_state = TD_REAL_TIME;
    ctx->mode_request = MODE_OFF;
    ctx->in_oil_pressure_ok = false;
    ctx->in_truck_ignition = false;
    ctx->evap_fan_always_on = false;
    ctx->op_state_previous = OP_OFF;
    ctx->attempted_start_counter = 0;
    ctx->external_temperature = 0;
    ctx->ext_temp_sensor_state = 0;
    ctx->engine_temp_ok = true;
    ctx->standby_override = false;
    ctx->cabin_temperature = 0;
    ctx->clmt_temp_setting = 0;
    ctx->evap_fan_speed = FAN_HIGH;
    ctx->compressor_on_timer = 0;
    ctx->compressor_off_timer = 0;
    ctx->refregerant_check_counter = 0;
    ctx->ac_low_pressure_ok = true;
    ctx->ac_high_pressure_ok = true;
    ctx->cool_mode = false;
    ctx->attempted_charging_counter = 0;
    ctx->battery_voltage = 0;
    ctx->batt_monitor_setting = 0;
    ctx->machine_run_min = 0;
    ctx->engine_run_min = 0;
    ctx->engine_oil_min = 0;
}

void control_deenergize_all(apu_ctx_t *ctx) {
    ctx->out.fuel_pump = false;  ctx->out.starter = false;  ctx->out.glow_plug = false;
    ctx->out.compressor_clutch = false;  ctx->out.heat_reverse = false;  ctx->out.evap_fan = false;
    ctx->out.condenser_fan = false;  ctx->out.condenser_duty = 0;
    ctx->cool_mode = false;
    ctx->engine_op_status = ST_OFF;  ctx->control_status = ST_OFF;
}

void control_register_mode(control_op_state_t st, control_mode_fn fn) {
    if (st < OP_STATE_COUNT) s_mode[st] = fn;
}

/* PIC UpdateSwitches: on a mode-request change, jump op_state. */
static void apply_mode_request(apu_ctx_t *ctx) {
    if (ctx->mode_request == s_mode_prev) return;
    s_mode_prev = ctx->mode_request;
    switch (ctx->mode_request) {
        case MODE_OFF:     ctx->op_state = OP_OFF;     ctx->error_state = ERR_NONE; ctx->sub_state = 0; break;
        case MODE_CLIMATE: ctx->op_state = OP_CLIMATE; ctx->sub_state = 0; break;
        case MODE_BATTERY: ctx->op_state = OP_BATTERY; ctx->sub_state = 0; break;
        default:           ctx->op_state = OP_OFF;     ctx->sub_state = 0; break;
    }
}

void control_tick(apu_ctx_t *ctx) {
    apply_mode_request(ctx);
    if (ctx->op_state < OP_STATE_COUNT && s_mode[ctx->op_state]) s_mode[ctx->op_state](ctx);
}
