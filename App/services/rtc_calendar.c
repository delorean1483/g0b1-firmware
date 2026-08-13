#include "rtc_calendar.h"
#include "nvm.h"
#include "nvm_map.h"

uint8_t rtc_cal_get_state(void)  { return nvm_read_byte(EE_CLND_START_ONOFF); }
void    rtc_cal_set_state(uint8_t v) { nvm_write_byte(EE_CLND_START_ONOFF, v); }
uint8_t rtc_cal_get_mode(void)   { return nvm_read_byte(EE_CLND_START_MODE); }
void    rtc_cal_set_mode(uint8_t v)  { nvm_write_byte(EE_CLND_START_MODE, v); }
uint8_t rtc_cal_get_year(void)   { return nvm_read_byte(EE_CLND_START_YEAR); }
void    rtc_cal_set_year(uint8_t v)  { nvm_write_byte(EE_CLND_START_YEAR, v); }
uint8_t rtc_cal_get_month(void)  { return nvm_read_byte(EE_CLND_START_MONTH); }
void    rtc_cal_set_month(uint8_t v) { nvm_write_byte(EE_CLND_START_MONTH, v); }
uint8_t rtc_cal_get_date(void)   { return nvm_read_byte(EE_CLND_START_DATE); }
void    rtc_cal_set_date(uint8_t v)  { nvm_write_byte(EE_CLND_START_DATE, v); }
uint8_t rtc_cal_get_hour(void)   { return nvm_read_byte(EE_CLND_START_HOUR); }
void    rtc_cal_set_hour(uint8_t v)  { nvm_write_byte(EE_CLND_START_HOUR, v); }
uint8_t rtc_cal_get_min(void)    { return nvm_read_byte(EE_CLND_START_MIN); }
void    rtc_cal_set_min(uint8_t v)   { nvm_write_byte(EE_CLND_START_MIN, v); }
uint8_t rtc_cal_get_ampm(void)   { return nvm_read_byte(EE_CLND_START_AMPM); }
void    rtc_cal_set_ampm(uint8_t v)  { nvm_write_byte(EE_CLND_START_AMPM, v); }
