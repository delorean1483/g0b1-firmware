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
