#include "control.h"

void control_off_mode(apu_ctx_t *ctx) {
    ctx->out = (apu_outputs_t){0};        /* all outputs off */
    ctx->engine_op_status = ST_OFF;
    ctx->control_status = ST_OFF;
    ctx->error_state = ERR_NONE;          /* OffMode resets all error state */
    ctx->evap_fan_always_on = false;
}
