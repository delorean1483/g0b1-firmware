#include "sensors.h"
#define SENSORS_CAL_OWNER   /* emit the lookup-table definitions in this TU */
#include "sensors_cal.h"

uint16_t sensors_battery_cv(uint16_t counts, uint16_t vref_cal) {
    uint32_t num = (uint32_t)counts * vref_cal * BATT_CV_NUM;
    return (uint16_t)((num + (BATT_CV_DEN / 2)) / BATT_CV_DEN); /* rounded */
}

/* Descending-count table linear interpolation (port of PIC FindNTCTemperature).
 * counts is expected to lie within (tbl[n-1][0], tbl[0][0]). */
static int16_t sens_interp(const int32_t tbl[][2], uint8_t n, int32_t counts) {
    uint8_t hi = 0;                 /* index of next-higher count entry */
    while (hi < (n - 1) && counts <= tbl[hi][0]) hi++;
    uint8_t lo = (hi > 0) ? (uint8_t)(hi - 1) : 0;
    int32_t span   = tbl[lo][0] - tbl[hi][0];      /* counts between entries */
    int32_t offset = tbl[lo][0] - counts;          /* counts from lower entry */
    int32_t dtemp  = tbl[hi][1] - tbl[lo][1];
    if (span == 0) return (int16_t)tbl[lo][1];
    return (int16_t)(tbl[lo][1] + (offset * dtemp) / span);
}

int16_t sensors_ext_temp_f(uint16_t counts, uint16_t vref_cal, uint8_t *sensor_state) {
    /* Calibration first (PIC ReadNTCTemperature order). */
    int32_t c = ((int32_t)counts * vref_cal) / VREF_CAL_DEFAULT;

    if (c >= NTC_DISCONNECT_CNT || c <= NTC_OVERMAX_CNT || c == NTC_SHORT_CNT) {
        *sensor_state = SENSOR_OFF;                /* disconnect / over-range / short */
        return 0;
    }
    *sensor_state = SENSOR_ON;
    if (c > NTC_FLOOR_CNT) return NTC_FLOOR_F;     /* colder than -4 degF => floor */
    return sens_interp(ext_ntc_table, EXT_NTC_TABLE_LEN, c);
}

int16_t sensors_encl_temp_f(uint16_t counts, int16_t temp_cal) {
    int16_t f = sens_interp(encl_tmp6131_table, ENCL_TABLE_LEN, (int32_t)counts);
    f = (int16_t)(f + temp_cal);
    if (f > TEMP_CLAMP_MAX_F) f = TEMP_CLAMP_MAX_F;
    if (f < TEMP_CLAMP_MIN_F) f = TEMP_CLAMP_MIN_F;
    return f;
}
