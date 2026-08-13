#ifndef SENSORS_H
#define SENSORS_H
#include "types.h"

#define SENSOR_OFF 0
#define SENSOR_ON  1

/* Battery voltage (centivolts) from an averaged ADC count and the reg-36 trim.
 * Rounded integer form of the schematic-derived 0.505517 cV/count. */
uint16_t sensors_battery_cv(uint16_t counts, uint16_t vref_cal);

/* External NTC temperature (degF). Applies the reg-36 trim to the count
 * (calibration first, as the PIC did), then table interpolation. Sets
 * *sensor_state ON/OFF (disconnect/short/over-range => OFF, 0 degF). */
int16_t sensors_ext_temp_f(uint16_t counts, uint16_t vref_cal, uint8_t *sensor_state);

/* Enclosure (on-board TMP6131) temperature (degF), + reg-37 offset trim,
 * clamped to [TEMP_CLAMP_MIN_F, TEMP_CLAMP_MAX_F]. */
int16_t sensors_encl_temp_f(uint16_t counts, int16_t temp_cal);

#endif /* SENSORS_H */
