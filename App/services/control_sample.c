#include "control.h"
#include "sensors.h"

void control_sample_sensors(apu_ctx_t *ctx) {
    ctx->external_temperature = sensors_get_ext_temp_f();
    ctx->ext_temp_sensor_state = sensors_get_ext_state();
    ctx->cabin_temperature = sensors_get_encl_temp_f();
    ctx->battery_voltage = sensors_get_batt_cv();
}
