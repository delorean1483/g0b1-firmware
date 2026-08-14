#include "control.h"

void control_error_shutdown_mode(apu_ctx_t *ctx) {
    ctx->temp_display_state = TD_REAL_TIME;
    switch (ctx->error_state) {
        case ERR_NONE:
            ctx->op_state = OP_OFF;
            ctx->sub_state = 0;
            break;
        case ERR_LOW_OIL:
        case ERR_HIGH_ENGINE_TEMP:
        case ERR_LOW_BATTERY:
        case ERR_STARTING_FAILURE:
        case ERR_NO_RPM_DETECTED:
            control_deenergize_all(ctx);   /* latching de-energize */
            break;
        case ERR_AC_LOW_PRESSURE:
        case ERR_AC_HIGH_PRESSURE:
            ctx->out.compressor_clutch = false;   /* engine keeps running */
            break;
        case ERR_STANDBY:
            control_deenergize_all(ctx);
            if (!ctx->in_truck_ignition || ctx->standby_override) {
                ctx->error_state = ERR_NONE;
                ctx->op_state = ctx->op_state_previous;
                ctx->sub_state = 0;
            }
            break;
        default:                            /* ERR_ENGINE_STALLED, ERR_HIGH_AC_PRESSURE, unmapped */
            ctx->op_state = OP_OFF;
            ctx->sub_state = 0;
            break;
    }
}
