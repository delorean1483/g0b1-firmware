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
