#include "rtc.h"

/* MCP7940N register map (verify bit positions vs datasheet — see plan Global Constraints). */
#define MCP_RTCSEC    0x00u
#define MCP_RTCMIN    0x01u
#define MCP_RTCHOUR   0x02u
#define MCP_RTCWKDAY  0x03u
#define MCP_RTCDATE   0x04u
#define MCP_RTCMTH    0x05u
#define MCP_RTCYEAR   0x06u
#define MCP_CONTROL   0x07u
#define MCP_OSCTRIM   0x08u
#define MCP_SRAM_BASE 0x20u
#define MCP_SRAM_SIZE 64u

#define MCP_ST_BIT    0x80u  /* RTCSEC  bit7: start oscillator            */
#define MCP_OSCRUN    0x20u  /* RTCWKDAY bit5: oscillator running (RO)     */
#define MCP_VBATEN    0x08u  /* RTCWKDAY bit3: enable battery backup       */
#define MCP_12_24_BIT 0x40u  /* RTCHOUR bit6: 1=12-hour mode (we use 24h)  */

uint8_t rtc_bcd_to_bin(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) & 0x0Fu) * 10u + (bcd & 0x0Fu));
}
uint8_t rtc_bin_to_bcd(uint8_t bin) {
    return (uint8_t)(((bin / 10u) << 4) | (bin % 10u));
}

static const i2c_backend_t *s_be;

void rtc_init(const i2c_backend_t *be) { s_be = be; }

int rtc_get_time(rtc_time_t *t) {
    uint8_t r[7];
    int rc = s_be->read(s_be->ctx, MCP_RTCSEC, r, 7);
    if (rc) return rc;
    t->sec     = rtc_bcd_to_bin((uint8_t)(r[0] & 0x7Fu));
    t->min     = rtc_bcd_to_bin((uint8_t)(r[1] & 0x7Fu));
    t->hour    = rtc_bcd_to_bin((uint8_t)(r[2] & 0x3Fu));  /* 24-hour */
    t->weekday = (uint8_t)(r[3] & 0x07u);
    t->date    = rtc_bcd_to_bin((uint8_t)(r[4] & 0x3Fu));
    t->month   = rtc_bcd_to_bin((uint8_t)(r[5] & 0x1Fu));
    t->year    = rtc_bcd_to_bin(r[6]);
    return 0;
}

int rtc_set_time(const rtc_time_t *t) {
    uint8_t r[7];
    r[0] = (uint8_t)(MCP_ST_BIT | rtc_bin_to_bcd(t->sec));   /* keep oscillator running */
    r[1] = rtc_bin_to_bcd(t->min);
    r[2] = rtc_bin_to_bcd(t->hour);                          /* 24-hour (bit6 = 0) */
    r[3] = (uint8_t)(MCP_VBATEN | (t->weekday & 0x07u));     /* enable battery backup */
    r[4] = rtc_bin_to_bcd(t->date);
    r[5] = rtc_bin_to_bcd(t->month);
    r[6] = rtc_bin_to_bcd(t->year);
    return s_be->write(s_be->ctx, MCP_RTCSEC, r, 7);
}

static int rtc_set_bits(uint8_t reg, uint8_t mask) {
    uint8_t v;
    int rc = s_be->read(s_be->ctx, reg, &v, 1);
    if (rc) return rc;
    v |= mask;
    return s_be->write(s_be->ctx, reg, &v, 1);
}

int rtc_osc_start(void)     { return rtc_set_bits(MCP_RTCSEC, MCP_ST_BIT); }
int rtc_backup_enable(void) { return rtc_set_bits(MCP_RTCWKDAY, MCP_VBATEN); }

bool rtc_osc_running(void) {
    uint8_t v = 0;
    if (s_be->read(s_be->ctx, MCP_RTCWKDAY, &v, 1)) return false;
    return (v & MCP_OSCRUN) != 0u;
}

int rtc_sram_read(uint8_t off, uint8_t *buf, uint16_t len) {
    if ((uint16_t)off + len > MCP_SRAM_SIZE) return -1;
    return s_be->read(s_be->ctx, (uint8_t)(MCP_SRAM_BASE + off), buf, len);
}
int rtc_sram_write(uint8_t off, const uint8_t *buf, uint16_t len) {
    if ((uint16_t)off + len > MCP_SRAM_SIZE) return -1;
    return s_be->write(s_be->ctx, (uint8_t)(MCP_SRAM_BASE + off), buf, len);
}
uint8_t rtc_reg52_read(void) {
    uint8_t v = 0;
    rtc_sram_read(0, &v, 1);
    return v;
}
