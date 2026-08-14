#include "control.h"
#include "app_timers.h"
#include "nvm.h"
#include "nvm_map.h"

#define HOURS_OIL_CHANGE_SOON    500u
#define HOURS_OIL_CHANGE_NOW     580u
#define HOURS_OIL_CHANGE_MISSED  700u
#define OIL_REWARN_SOON          1200u   /* 20 hr */
#define OIL_REWARN_PAST_DUE      300u    /* 5 hr  */

static void bump_hour(uint16_t addr) {
    uint16_t w = nvm_read_word(addr);
    if (w < 65535u) nvm_write_word(addr, (uint16_t)(w + 1u));
}

void control_oil_change_check(apu_ctx_t *ctx) {
    uint16_t hours = nvm_read_word(ENGINE_OILTIME_START);
    if (hours < HOURS_OIL_CHANGE_SOON) {
        ctx->oil_change_state = OIL_GOOD;
    } else if (hours < HOURS_OIL_CHANGE_NOW && app_timer_expired(SCALE_MINUTE, NEXT_OIL_WARNING_TMR)) {
        if (ctx->oil_change_state != OIL_WARNING_DISMISSED) ctx->oil_change_state = OIL_CHANGE_SOON;
        else app_timer_set(SCALE_MINUTE, NEXT_OIL_WARNING_TMR, OIL_REWARN_SOON);
    } else if (hours < HOURS_OIL_CHANGE_MISSED && app_timer_expired(SCALE_MINUTE, NEXT_OIL_WARNING_TMR)) {
        if (ctx->oil_change_state != OIL_WARNING_DISMISSED) ctx->oil_change_state = OIL_CHANGE_NEEDED;
        else app_timer_set(SCALE_MINUTE, NEXT_OIL_WARNING_TMR, OIL_REWARN_SOON);
    } else if (app_timer_expired(SCALE_MINUTE, NEXT_OIL_WARNING_TMR)) {   /* hours >= 700 */
        if (ctx->oil_change_state != OIL_WARNING_DISMISSED) ctx->oil_change_state = OIL_CHANGE_PAST_DUE;
        else app_timer_set(SCALE_MINUTE, NEXT_OIL_WARNING_TMR, OIL_REWARN_PAST_DUE);
    }
}

void control_service_runtime(apu_ctx_t *ctx) {
    /* Machine hours: always. */
    ctx->machine_run_min++;
    if (ctx->machine_run_min >= 60u) {
        ctx->machine_run_min = 0;
        bump_hour(MACHINE_RUNTIME_START);
    }
    /* Engine + oil hours: only while the engine is running. */
    if (ctx->out.fuel_pump) {
        ctx->engine_run_min++;
        if (ctx->engine_run_min >= 60u) {
            ctx->engine_run_min = 0;
            bump_hour(ENGINE_RUNTIME_START);
        }
        ctx->engine_oil_min++;
        if (ctx->engine_oil_min >= 60u) {
            ctx->engine_oil_min = 0;
            bump_hour(ENGINE_OILTIME_START);
            control_oil_change_check(ctx);
        }
    }
}
