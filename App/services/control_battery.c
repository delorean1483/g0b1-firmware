#include "control.h"
#include "nvm.h"
#include "nvm_map.h"

void control_battery_sample_settings(apu_ctx_t *ctx) {
    ctx->batt_monitor_setting = nvm_read_word(EE_MONITOR_BATT_SETTING);
}
