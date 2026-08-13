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

void rtc_init(const i2c_backend_t *be);
int  rtc_get_time(rtc_time_t *t);
int  rtc_set_time(const rtc_time_t *t);

int     rtc_osc_start(void);
int     rtc_backup_enable(void);
bool    rtc_osc_running(void);
int     rtc_sram_read(uint8_t off, uint8_t *buf, uint16_t len);
int     rtc_sram_write(uint8_t off, const uint8_t *buf, uint16_t len);
uint8_t rtc_reg52_read(void);

void rtcc_set_year(uint16_t v);
void rtcc_set_month(uint16_t v);
void rtcc_set_day(uint16_t v);
void rtcc_set_weekday(uint16_t v);
void rtcc_set_hour(uint16_t v);
void rtcc_set_minute(uint16_t v);
void rtcc_set_second(uint16_t v);
int  rtcc_commit(void);
uint16_t rtcc_get_year(void);
uint16_t rtcc_get_month(void);
uint16_t rtcc_get_day(void);
uint16_t rtcc_get_weekday(void);
uint16_t rtcc_get_hour(void);
uint16_t rtcc_get_minute(void);
uint16_t rtcc_get_second(void);

#endif /* RTC_H */
