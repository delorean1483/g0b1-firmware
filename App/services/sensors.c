#include "sensors.h"
#define SENSORS_CAL_OWNER   /* emit the lookup-table definitions in this TU */
#include "sensors_cal.h"

typedef struct {
    uint32_t accum;
    uint16_t count;
    uint16_t window;      /* samples per average */
    uint16_t average;     /* last completed rolling average (raw counts) */
} sens_chan_t;

static sens_chan_t s_chan[SENS_CH_COUNT];
static uint16_t s_vref_cal;
static int16_t  s_temp_cal;
static uint16_t s_batt_cv;
static int16_t  s_encl_f;
static int16_t  s_ext_f;
static uint8_t  s_ext_state;

void sensors_set_cal(uint16_t vref_cal, int16_t temp_cal) {
    s_vref_cal = vref_cal;
    s_temp_cal = temp_cal;
}

void sensors_init(uint16_t vref_cal, int16_t temp_cal) {
    for (int i = 0; i < SENS_CH_COUNT; i++) {
        s_chan[i].accum = 0;
        s_chan[i].count = 0;
        s_chan[i].average = 0;
        s_chan[i].window = SENS_AVG_DEFAULT;
    }
    s_chan[SENS_ENCL].window = SENS_AVG_ENCL;
    s_batt_cv = 0; s_encl_f = 0; s_ext_f = 0; s_ext_state = SENSOR_OFF;
    sensors_set_cal(vref_cal, temp_cal);
}

void sensors_add_sample(sensor_ch_t ch, uint16_t raw) {
    sens_chan_t *c = &s_chan[ch];
    c->accum += raw;
    if (++c->count < c->window) return;
    c->average = (uint16_t)(c->accum / c->window);
    c->accum = 0;
    c->count = 0;
    switch (ch) {
        case SENS_ENCL: s_encl_f = sensors_encl_temp_f(c->average, s_temp_cal); break;
        case SENS_EXT:  s_ext_f  = sensors_ext_temp_f(c->average, s_vref_cal, &s_ext_state); break;
        case SENS_BATT: s_batt_cv = sensors_battery_cv(c->average, s_vref_cal); break;
        default: break;
    }
}

int16_t  sensors_get_encl_temp_f(void) { return s_encl_f; }
uint16_t sensors_get_ext_adc(void)     { return s_chan[SENS_EXT].average; }
uint16_t sensors_get_batt_cv(void)     { return s_batt_cv; }
int16_t  sensors_get_ext_temp_f(void)  { return s_ext_f; }
uint8_t  sensors_get_ext_state(void)   { return s_ext_state; }

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
