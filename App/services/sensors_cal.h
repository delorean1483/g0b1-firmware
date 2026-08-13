#ifndef SENSORS_CAL_H
#define SENSORS_CAL_H
/* All sensor conversion coefficients and lookup tables live here so the
 * OI-7 bench-calibration pass edits numbers only. Derived from
 * "G0B1 APU Manager R1.pdf" p.3 (new front-end) and
 * "6513F000-99SCH.pdf" p.4-5 (old PIC front-end).
 *
 * The lookup-TABLE definitions (added in Tasks 2-3) are emitted only in the
 * TU that #defines SENSORS_CAL_OWNER before including this header (sensors.c).
 * Every other includer (test files, rpm.c) sees the scalar #defines only, so
 * an unused static-const table never trips -Werror,-Wunused-const-variable. */
#include "types.h"

/* ADC front-end: 12-bit, external 3.000 V precision reference. */
#define ADC_FULL_SCALE_CNT  4096
#define ADC_VREF_MV         3000
#define VREF_CAL_DEFAULT    250     /* reg 36 nominal (PIC VREF_CAL_INIT) */

/* Battery (IN3): 30.1k/5.1k divider (0.144886) + unity buffer (U8A).
 * batt_cV = round(counts * vref_cal * BATT_CV_NUM / BATT_CV_DEN).
 * At vref_cal=250 => 0.505517 cV/count (2176->1100, 2374->1200, 2868->1450). */
#define BATT_CV_NUM   2
#define BATT_CV_DEN   989

/* External NTC (IN4): old Kohler 14-point table (10-bit counts -> degF), with
 * the count column rescaled x4.4 for the new 12-bit / 3.3V-excited / 3.000V-ref
 * front-end (see plan "Derived conversions"). Descending by count. */
#define EXT_NTC_TABLE_LEN   14
/* F1: open sensor saturates at 4095 (old 1020->4488 > ADC max), so disconnect
 * is only detectable at rail saturation. Provisional; confirm on bench. */
#define NTC_DISCONNECT_CNT  4090   /* >= this  => disconnected/too-cold => OFF, 0 degF */
#define NTC_SHORT_CNT       0      /* == this  => shorted => OFF, 0 degF */
#define NTC_FLOOR_CNT       3709   /* >  this (and < disconnect) => floor at -4 degF, ON */
#define NTC_FLOOR_F         (-4)
#define NTC_OVERMAX_CNT     238    /* <= this  => over max temp (>248 degF) => OFF, 0 degF */
#ifdef SENSORS_CAL_OWNER
static const int32_t ext_ntc_table[EXT_NTC_TABLE_LEN][2] = {
    {4488,   0}, {3709,  -4}, {2548,  20}, {1971,  32},
    {1518,  50}, {1074,  68}, { 924,  80}, { 774,  92},
    { 629, 104}, { 537, 120}, { 422, 140}, { 312, 176},
    { 264, 212}, { 238, 248},
};
#endif

#endif /* SENSORS_CAL_H */
