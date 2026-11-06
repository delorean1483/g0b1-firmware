#ifndef BENCH_I2C2_PROFILE_H
#define BENCH_I2C2_PROFILE_H
/* Matched bench builds: only bus timing, client timeout and version differ.
 * Define G0B1_BENCH_I2C2_100KHZ=1 for the original 100 kHz / 10 ms profile. */
#ifndef G0B1_BENCH_I2C2_100KHZ
#define G0B1_BENCH_I2C2_100KHZ 0
#endif
#if G0B1_BENCH_I2C2_100KHZ
#define BENCH_I2C2_TIMING 0x60302730u
#define BENCH_I2C2_TIMEOUT_MS 10u
#define BENCH_I2C2_VERSION_PATCH 94u
#else
#define BENCH_I2C2_TIMING 0xF0102527u
#define BENCH_I2C2_TIMEOUT_MS 20u
#define BENCH_I2C2_VERSION_PATCH 93u
#endif
#endif
