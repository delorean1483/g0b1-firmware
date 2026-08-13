#ifndef SENSORS_H
#define SENSORS_H
#include "types.h"

/* Battery voltage (centivolts) from an averaged ADC count and the reg-36 trim.
 * Rounded integer form of the schematic-derived 0.505517 cV/count. */
uint16_t sensors_battery_cv(uint16_t counts, uint16_t vref_cal);

#endif /* SENSORS_H */
