#include "control.h"
#include "app_timers.h"

void control_powerup_mode(apu_ctx_t *ctx) {
    switch (ctx->sub_state) {
        case 0:
            ctx->out = (apu_outputs_t){0};                 /* all outputs off */
            ctx->engine_op_status = ST_OFF;
            ctx->control_status = ST_OFF;
            app_timer_set(SCALE_SECOND, POWER_UP_TMR, 1);  /* PIC literal (stale "5 s" comment) */
            app_timer_set(SCALE_MINUTE, CABIN_TEMP_WARMUP_TMR, 10);
            ctx->sub_state = 1;
            break;
        case 1:
            if (app_timer_expired(SCALE_SECOND, POWER_UP_TMR)) {
                ctx->op_state = OP_OFF;
                ctx->sub_state = 0;
            }
            break;
        default:
            ctx->op_state = OP_OFF;
            ctx->sub_state = 0;
            break;
    }
}
