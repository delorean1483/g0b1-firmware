#include "control.h"
#include "app_timers.h"
#include "board_pins.h"
#include "mb_regmodel.h"

#define DIAG_INACTIVITY_SEC 10u

static apu_ctx_t *s_ctx;
static int8_t     s_active = -1;   /* -1 = none, else bsp_out_t index */

/* Refuse energizing the APU while the truck engine is on (no override). */
static bool diag_engine_gate_ok(const apu_ctx_t *ctx) {
    return !(ctx->in_truck_ignition && !ctx->standby_override);
}
static bool diag_entry_ok(const apu_ctx_t *ctx) {
    return ctx->op_state == OP_OFF
        && ctx->engine_op_status == ST_OFF
        && !ctx->out.fuel_pump
        && diag_engine_gate_ok(ctx);
}

static void diag_enter(apu_ctx_t *ctx) {
    control_deenergize_all(ctx);
    s_active = -1;
    ctx->op_state = OP_DIAG;
    app_timer_set(SCALE_SECOND, DIAG_INACTIVITY_TMR, DIAG_INACTIVITY_SEC);
}
static void diag_exit(apu_ctx_t *ctx) {
    control_deenergize_all(ctx);
    s_active = -1;
    ctx->op_state = OP_OFF;
}

static modbus_exc_t rd_diag_mode(uint16_t r, uint16_t *o) {
    (void)r; *o = (s_ctx->op_state == OP_DIAG) ? 1u : 0u; return MB_EXC_NONE;
}
static modbus_exc_t wr_diag_mode(uint16_t r, uint16_t v) {
    (void)r;
    if (v == 0u) { diag_exit(s_ctx); return MB_EXC_NONE; }
    if (v != 1u) return MB_EXC_ILLEGAL_VALUE;
    if (s_ctx->op_state == OP_DIAG) {   /* keepalive */
        app_timer_set(SCALE_SECOND, DIAG_INACTIVITY_TMR, DIAG_INACTIVITY_SEC);
        return MB_EXC_NONE;
    }
    if (!diag_entry_ok(s_ctx)) return MB_EXC_ILLEGAL_VALUE;
    diag_enter(s_ctx);
    return MB_EXC_NONE;
}

static bool diag_is_engine(int8_t idx) { return idx >= 0 && idx <= OUT_GLOW_PLUG; }

/* Map the single active output onto ctx->out (called from de-energized state). */
static void diag_apply_active(apu_ctx_t *ctx) {
    switch (s_active) {
        case OUT_FUEL_PUMP:         ctx->out.fuel_pump = true; break;
        case OUT_STARTER:           ctx->out.starter = true; break;
        case OUT_GLOW_PLUG:         ctx->out.glow_plug = true; break;
        case OUT_COMPRESSOR_CLUTCH: ctx->out.compressor_clutch = true; break;
        case OUT_HEAT_REVERSER:     ctx->out.heat_reverse = true; break;
        case OUT_EVAP_FAN:          ctx->out.evap_fan = true; ctx->out.evap_speed = 100u; break;
        case OUT_CONDENSER_FAN:     ctx->out.condenser_fan = true; ctx->out.condenser_duty = 1000u; break;
        default: break;
    }
}

static modbus_exc_t rd_diag_status(uint16_t r, uint16_t *o) {
    (void)r;
    *o = (s_ctx->op_state == OP_DIAG && s_active >= 0) ? (uint16_t)(1u << s_active) : 0u;
    return MB_EXC_NONE;
}

static modbus_exc_t wr_diag_out(uint16_t r, uint16_t v) {
    (void)r;
    if (s_ctx->op_state != OP_DIAG) return MB_EXC_ILLEGAL_VALUE;
    uint8_t idx   = (uint8_t)(v >> 8);
    uint8_t state = (uint8_t)(v & 0xFFu);
    if (idx >= OUT_COUNT) return MB_EXC_ILLEGAL_VALUE;
    if (diag_is_engine((int8_t)idx) && !diag_engine_gate_ok(s_ctx)) return MB_EXC_ILLEGAL_VALUE;
    if (state) {
        bool rising = (s_active != (int8_t)idx);
        s_active = (int8_t)idx;
        if (diag_is_engine(s_active) && rising)
            app_timer_set(SCALE_SECOND, DIAG_ENGINE_TMR, (s_active == OUT_STARTER) ? 4u : 5u);
    } else if (s_active == (int8_t)idx) {
        s_active = -1;
    }
    app_timer_set(SCALE_SECOND, DIAG_INACTIVITY_TMR, DIAG_INACTIVITY_SEC);
    return MB_EXC_NONE;
}

void control_diag_register(apu_ctx_t *ctx) {
    s_ctx = ctx;
    s_active = -1;
    mb_reg_bind(49, rd_diag_mode,   wr_diag_mode);
    mb_reg_bind(50, 0,              wr_diag_out);
    mb_reg_bind(41, rd_diag_status, 0);
}

void control_diag_mode(apu_ctx_t *ctx) {
    if (app_timer_expired(SCALE_SECOND, DIAG_INACTIVITY_TMR)) { diag_exit(ctx); return; }
    if (diag_is_engine(s_active) &&
        (app_timer_expired(SCALE_SECOND, DIAG_ENGINE_TMR) || !diag_engine_gate_ok(ctx)))
        s_active = -1;
    control_deenergize_all(ctx);   /* single-active: clear, then apply the one */
    diag_apply_active(ctx);
}
