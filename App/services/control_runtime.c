#include "control.h"
#include "app_timers.h"
#include "nvm.h"
#include "nvm_map.h"

#define HOURS_OIL_CHANGE_SOON    500u
#define HOURS_OIL_CHANGE_NOW     580u
#define HOURS_OIL_CHANGE_MISSED  700u
#define OIL_REWARN_SOON          1200u   /* 20 hr */
#define OIL_REWARN_PAST_DUE      300u    /* 5 hr  */

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
