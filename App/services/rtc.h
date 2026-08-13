#ifndef RTC_H
#define RTC_H
#include "types.h"
#include "i2c_backend.h"

/* 24-hour time. year is 0..99 (=> 20xx). weekday 1..7, date 1..31, month 1..12. */
typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;     /* 0..23 */
    uint8_t weekday;  /* 1..7  */
    uint8_t date;     /* 1..31 */
    uint8_t month;    /* 1..12 */
    uint8_t year;     /* 0..99 */
} rtc_time_t;

uint8_t rtc_bcd_to_bin(uint8_t bcd);
uint8_t rtc_bin_to_bcd(uint8_t bin);

#endif /* RTC_H */
