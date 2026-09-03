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

/* Engine-coolant sender on the AN_Ext_Temp net (PA4 / ADC1_IN4).
 * Robb's board front-end (2026-09-02): a low-side NTC sender with a 197.9 ohm
 * pull-up to 3.300 V (the 10k stock pull-up was changed to 200 ohm for this
 * low-R sender), read against the 3.000 V ADC reference, 12-bit. Temperature via
 * Steinhart-Hart with the sender's A/B/C coefficients (supplied by Robb):
 *   V = count*VREF/4095 ; R = PULLUP*V/(PULLUP_V - V) ; ln = ln(R)
 *   1/T(K) = A + B*ln + C*ln^3 ; degF = (T-273.15)*9/5 + 32
 * Note: VREF (3.000) < PULLUP_V (3.300), so the ADC can never reach the rail;
 * only a dead short to ground (count 0 -> 0 V) is flagged as a fault. */
#define COOLANT_ADC_MAX      4095.0f
#define COOLANT_ADC_VREF     3.000f
#define COOLANT_PULLUP_V     3.300f
#define COOLANT_PULLUP_OHMS  197.9f
#define COOLANT_SH_A         1.71206530e-3f
#define COOLANT_SH_B         2.68030415e-4f
#define COOLANT_SH_C         (-2.00829453e-7f)
#define COOLANT_FAULT_F      (-460)   /* open/short sentinel (below absolute zero degF) */

/* Enclosure (IN0): TMP6131 (TI linear silicon PTC, 10k @ 25C) with a 10k fixed
 * top resistor, ratiometric to the 3.000 V ref. count = 4096*S/(S+1), S=R/R25.
 * degF from the TMP6131 R/R25-vs-T curve. R/R25 values are first-cut from the
 * TMP61 datasheet (SNIS183) and MUST be verified (F3); 25C=2048cnt is exact.
 * Descending by count for sens_interp(). */
#define ENCL_TABLE_LEN  6
/* Preserve PIC Find_Cabin_Temperature clamp. */
#define TEMP_CLAMP_MAX_F   302
#define TEMP_CLAMP_MIN_F   (-67)
#ifdef SENSORS_CAL_OWNER
static const int32_t encl_tmp6131_table[ENCL_TABLE_LEN][2] = {
    /* count, degF   (S=R/R25 -> degC -> degF) */
    {2533, 257},  /* S~1.62, 125 C */
    {2361, 185},  /* S~1.36,  85 C */
    {2191, 122},  /* S~1.15,  50 C */
    {2048,  77},  /* S=1.00,  25 C  (exact anchor) */
    {1894,  32},  /* S~0.86,   0 C */
    {1665, -40},  /* S~0.685,-40 C */
};
#endif

/* Averaging (preserve PIC: enclosure 8-sample; others N-sample).
 * Per-channel N for non-enclosure channels is a carry-forward to confirm
 * against the original analog_data[].readings_to_average init. */
#define SENS_AVG_ENCL      8
#define SENS_AVG_DEFAULT   8

/* Engine-running threshold (PIC main.h ENGINE_RPM_LOW_LIMIT). */
#define ENGINE_RPM_LOW_LIMIT  1000

#endif /* SENSORS_CAL_H */
