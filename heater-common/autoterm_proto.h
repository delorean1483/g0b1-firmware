#ifndef AUTOTERM_PROTO_H
#define AUTOTERM_PROTO_H
/* AUTOTERM 4-wire serial protocol: framing, CRC, request builders, reply
   parser and status decode. Pure functions, no timing (see drv_autoterm).

   Frame: AA dir LEN 00 CMD payload[LEN] CRC_HI CRC_LO
   dir 0x03 = controller->heater, 0x04 = heater->controller.
   CRC-16/MODBUS over AA..payload, transmitted HIGH byte first. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AT_PREAMBLE      0xAAu
#define AT_DIR_REQ       0x03u
#define AT_DIR_RESP      0x04u

#define AT_CMD_START     0x01u
#define AT_CMD_SETTINGS  0x02u
#define AT_CMD_STOP      0x03u
#define AT_CMD_VERSION   0x06u
#define AT_CMD_STATUS    0x0Fu
#define AT_CMD_TEMP      0x11u
#define AT_CMD_WAKE      0x1Cu

#define AT_MAX_PAYLOAD   32u
#define AT_OVERHEAD      7u      /* AA dir len 00 cmd crc_hi crc_lo */
#define AT_MAX_FRAME     (AT_OVERHEAD + AT_MAX_PAYLOAD)

#define AT_STATUS_LEN    19u

/* Builders return the frame length written to out (out must hold it). */
size_t AT_BuildFrame(uint8_t dir, uint8_t cmd, const uint8_t *payload,
                     uint8_t len, uint8_t *out);
size_t AT_BuildSimple(uint8_t cmd, uint8_t out[AT_OVERHEAD]);   /* empty payload */
size_t AT_BuildTemp(int8_t temp_c, uint8_t out[8]);
size_t AT_BuildStart(int8_t setpoint_c, uint8_t out[14]);
size_t AT_BuildSettingsWrite(int8_t setpoint_c, uint8_t out[13]);

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[AT_MAX_PAYLOAD];
} at_frame_t;

typedef struct {
    uint8_t  buf[AT_MAX_FRAME];
    uint8_t  idx;
    uint8_t  total;
    uint32_t crc_fails;
} at_parser_t;

void AT_ParserReset(at_parser_t *p);
/* Feed one byte. Returns true when a CRC-valid heater reply (dir 0x04)
   completes; it is copied to *out. Resyncs on AA after any error. */
bool AT_ParseByte(at_parser_t *p, uint8_t byte, at_frame_t *out);

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    int8_t   internal_c;        /* provisional */
    uint8_t  external_c;        /* 0x7F = no sensor */
    uint16_t supply_mv;         /* verified: byte6 x 100 mV */
    uint16_t exch_raw;          /* raw u16, big-endian bytes 7-8 */
    uint8_t  fan_rps_req;       /* provisional */
    uint8_t  fan_rps;           /* provisional */
    uint8_t  pump_hz_x100_req;  /* provisional */
    uint8_t  pump_hz_x100;      /* provisional */
} at_status_t;

/* Decode a 0x0F status reply. False if cmd/len don't match. */
bool AT_DecodeStatus(const at_frame_t *f, at_status_t *out);

/* htr_hint_t for a reported state (evidence: autoterm_cycle.csv).
   0.x off; 1.x and 2.0-2.3 starting; 2.4+ and 3.x running; 4.x cooldown. */
uint8_t AT_PhaseHint(uint8_t major, uint8_t minor);

#endif /* AUTOTERM_PROTO_H */
