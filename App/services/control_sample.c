#include "control.h"
#include "sensors.h"

void control_sample_sensors(apu_ctx_t *ctx) {
    /* The AN_Ext_Temp net (PA4) now carries the coolant sender, not an outside-air
     * NTC, so glow-plug cold-start timing keys off the on-board enclosure PTC as
     * the ambient proxy (a real, always-present sensor; enclosure ~ ambient when
     * the unit has been sitting, which is the cold-start case). The enclosure
     * sensor never disconnects, so its state is always ON. */
    ctx->external_temperature = sensors_get_encl_temp_f();
    ctx->ext_temp_sensor_state = SENSOR_ON;
    ctx->cabin_temperature = sensors_get_encl_temp_f();
    ctx->battery_voltage = sensors_get_batt_cv();
}
