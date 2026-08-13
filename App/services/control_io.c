#include "control.h"
#include "bsp_io.h"
#include "io_debounce.h"
#include "mb_regmodel.h"

static discrete_input_t s_oil, s_ign;
static apu_ctx_t *s_ctx;

void control_inputs_init(apu_ctx_t *ctx) {
    io_debounce_init(&s_oil, CONTROL_INPUT_DEBOUNCE_TIME, SWITCH_OPEN);
    io_debounce_init(&s_ign, CONTROL_INPUT_DEBOUNCE_TIME, SWITCH_OPEN);
    ctx->in_oil_pressure_ok = false;
    ctx->in_truck_ignition = false;
}

void control_inputs_service(apu_ctx_t *ctx) {
    io_debounce_service(&s_oil, bsp_in_read(IN_OIL_PRESSURE) ? SWITCH_CLOSED : SWITCH_OPEN);
    io_debounce_service(&s_ign, bsp_in_read(IN_TRUCK_IGNITION) ? SWITCH_CLOSED : SWITCH_OPEN);
    ctx->in_oil_pressure_ok = (io_debounce_state(&s_oil) == SWITCH_CLOSED);
    ctx->in_truck_ignition  = (io_debounce_state(&s_ign) == SWITCH_CLOSED);
}

/* ---- register accessors (module-static ctx) ---- */
static modbus_exc_t rd_oil(uint16_t r, uint16_t *o)   { (void)r; *o = s_ctx->in_oil_pressure_ok; return MB_EXC_NONE; }
static modbus_exc_t rd_ign(uint16_t r, uint16_t *o)   { (void)r; *o = s_ctx->in_truck_ignition;  return MB_EXC_NONE; }
static modbus_exc_t rd_mode(uint16_t r, uint16_t *o)  { (void)r; *o = s_ctx->mode_request;        return MB_EXC_NONE; }
static modbus_exc_t wr_mode(uint16_t r, uint16_t v)   { (void)r; if (v > MODE_BATTERY) return MB_EXC_ILLEGAL_VALUE; s_ctx->mode_request = (uint8_t)v; return MB_EXC_NONE; }
static modbus_exc_t rd_err(uint16_t r, uint16_t *o)   { (void)r; *o = s_ctx->error_state;         return MB_EXC_NONE; }
static modbus_exc_t rd_oilc(uint16_t r, uint16_t *o)  { (void)r; *o = s_ctx->oil_change_state;    return MB_EXC_NONE; }
static modbus_exc_t rd_eng(uint16_t r, uint16_t *o)   { (void)r; *o = s_ctx->engine_op_status;    return MB_EXC_NONE; }
static modbus_exc_t rd_ctrl(uint16_t r, uint16_t *o)  { (void)r; *o = s_ctx->control_status;      return MB_EXC_NONE; }
static modbus_exc_t rd_td(uint16_t r, uint16_t *o)    { (void)r; *o = s_ctx->temp_display_state;  return MB_EXC_NONE; }
static modbus_exc_t wr_td(uint16_t r, uint16_t v)     { (void)r; if (v > TD_CS_SETTING) return MB_EXC_ILLEGAL_VALUE; s_ctx->temp_display_state = (uint8_t)v; return MB_EXC_NONE; }

void control_regs_register(apu_ctx_t *ctx) {
    s_ctx = ctx;
    mb_reg_bind(7,  rd_oil,  0);
    mb_reg_bind(8,  rd_ign,  0);
    mb_reg_bind(10, rd_mode, wr_mode);
    mb_reg_bind(17, rd_err,  0);
    mb_reg_bind(18, rd_oilc, 0);
    mb_reg_bind(22, rd_eng,  0);
    mb_reg_bind(23, rd_ctrl, 0);
    mb_reg_bind(33, rd_td,   wr_td);
}
