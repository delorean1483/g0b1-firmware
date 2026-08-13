#include "sensors.h"
#define SENSORS_CAL_OWNER   /* emit the lookup-table definitions in this TU */
#include "sensors_cal.h"

uint16_t sensors_battery_cv(uint16_t counts, uint16_t vref_cal) {
    uint32_t num = (uint32_t)counts * vref_cal * BATT_CV_NUM;
    return (uint16_t)((num + (BATT_CV_DEN / 2)) / BATT_CV_DEN); /* rounded */
}
