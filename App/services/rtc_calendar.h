#ifndef RTC_CALENDAR_H
#define RTC_CALENDAR_H
#include "types.h"
/* Calendar-start parameters (Modbus regs 24-31), stored as raw bytes in NVM at
   the EE_CLND_START_* addresses. The control layer (M6) interprets them against
   the live clock to decide auto-start; these accessors are pure storage. */
uint8_t rtc_cal_get_state(void);   void rtc_cal_set_state(uint8_t v);   /* reg 24 on/off */
uint8_t rtc_cal_get_mode(void);    void rtc_cal_set_mode(uint8_t v);    /* reg 25 */
uint8_t rtc_cal_get_year(void);    void rtc_cal_set_year(uint8_t v);    /* reg 26 */
uint8_t rtc_cal_get_month(void);   void rtc_cal_set_month(uint8_t v);   /* reg 27 */
uint8_t rtc_cal_get_date(void);    void rtc_cal_set_date(uint8_t v);    /* reg 28 */
uint8_t rtc_cal_get_hour(void);    void rtc_cal_set_hour(uint8_t v);    /* reg 29 */
uint8_t rtc_cal_get_min(void);     void rtc_cal_set_min(uint8_t v);     /* reg 30 */
uint8_t rtc_cal_get_ampm(void);    void rtc_cal_set_ampm(uint8_t v);    /* reg 31 */
#endif /* RTC_CALENDAR_H */
