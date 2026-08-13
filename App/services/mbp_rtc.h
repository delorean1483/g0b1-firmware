#ifndef MBP_RTC_H
#define MBP_RTC_H
/* Register the RTC/calendar providers (Modbus regs 24-31 calendar, 42-48 RTCC, 52 SRAM).
   rtc_init(...) must have been called first so the RTCC read/write path has a backend. */
void mbp_rtc_register(void);
#endif /* MBP_RTC_H */
