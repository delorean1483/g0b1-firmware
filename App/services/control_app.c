#include "control.h"

static apu_ctx_t s_ctx;

apu_ctx_t *control_app_ctx(void) { return &s_ctx; }

void control_app_init(void) {
    control_init(&s_ctx);
    control_inputs_init(&s_ctx);
    control_register_mode(OP_POWER_UP, control_powerup_mode);
    control_register_mode(OP_OFF,      control_off_mode);
    control_register_mode(OP_ENGINE_START, control_engine_start_mode);
    control_regs_register(&s_ctx);
}

void control_10ms_slot(void) {
    control_sample_sensors(&s_ctx);
    control_inputs_service(&s_ctx);
    control_tick(&s_ctx);
    outputs_apply(&s_ctx);
}
