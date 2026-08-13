#include "mbp_rtc.h"
#include "mb_regmodel.h"
#include "rtc.h"
#include "rtc_calendar.h"

/* ---- calendar (regs 24-31): raw byte pass-through to NVM ---- */
static modbus_exc_t rd_cal(uint16_t reg, uint16_t *o) {
    switch (reg) {
        case 24: *o = rtc_cal_get_state(); break;  case 25: *o = rtc_cal_get_mode(); break;
        case 26: *o = rtc_cal_get_year(); break;   case 27: *o = rtc_cal_get_month(); break;
        case 28: *o = rtc_cal_get_date(); break;   case 29: *o = rtc_cal_get_hour(); break;
        case 30: *o = rtc_cal_get_min(); break;    case 31: *o = rtc_cal_get_ampm(); break;
        default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    return MB_EXC_NONE;
}
static modbus_exc_t wr_cal(uint16_t reg, uint16_t v) {
    uint8_t b = (uint8_t)v;
    switch (reg) {
        case 24: rtc_cal_set_state(b); break;  case 25: rtc_cal_set_mode(b); break;
        case 26: rtc_cal_set_year(b); break;   case 27: rtc_cal_set_month(b); break;
        case 28: rtc_cal_set_date(b); break;   case 29: rtc_cal_set_hour(b); break;
        case 30: rtc_cal_set_min(b); break;    case 31: rtc_cal_set_ampm(b); break;
        default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    return MB_EXC_NONE;
}

/* ---- RTCC (regs 42-48): read live clock; write seeds stage from live then commits ---- */
static modbus_exc_t rd_rtcc(uint16_t reg, uint16_t *o) {
    rtc_time_t t = {0};
    rtc_get_time(&t);
    switch (reg) {
        case 42: *o = t.year; break;   case 43: *o = t.month; break;  case 44: *o = t.date; break;
        case 45: *o = t.weekday; break;case 46: *o = t.hour; break;   case 47: *o = t.min; break;
        case 48: *o = t.sec; break;    default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    return MB_EXC_NONE;
}
static modbus_exc_t wr_rtcc(uint16_t reg, uint16_t v) {
    rtc_time_t t = {0};
    rtc_get_time(&t);                              /* seed stage from live clock (M4a seam) */
    rtcc_set_year(t.year);   rtcc_set_month(t.month);   rtcc_set_day(t.date);
    rtcc_set_weekday(t.weekday); rtcc_set_hour(t.hour); rtcc_set_minute(t.min);
    rtcc_set_second(t.sec);
    switch (reg) {                                 /* override the one field being written */
        case 42: rtcc_set_year(v); break;    case 43: rtcc_set_month(v); break;
        case 44: rtcc_set_day(v); break;     case 45: rtcc_set_weekday(v); break;
        case 46: rtcc_set_hour(v); break;    case 47: rtcc_set_minute(v); break;
        case 48: rtcc_set_second(v); break;  default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    rtcc_commit();
    return MB_EXC_NONE;
}

/* ---- reg 52: battery-backed SRAM offset 0 (read-only) ---- */
static modbus_exc_t rd_reg52(uint16_t reg, uint16_t *o) { (void)reg; *o = rtc_reg52_read(); return MB_EXC_NONE; }

void mbp_rtc_register(void) {
    for (uint16_t r = 24; r <= 31; r++) mb_reg_bind(r, rd_cal, wr_cal);
    for (uint16_t r = 42; r <= 48; r++) mb_reg_bind(r, rd_rtcc, wr_rtcc);
    mb_reg_bind(52, rd_reg52, 0);
}
