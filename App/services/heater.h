#ifndef HEATER_H
#define HEATER_H
/* G0B1 heater service: I2C client of the heater coprocessor (0x64, map
   heater-common/htr_i2c_map.h). The coprocessor owns every heater protocol
   and the start/run/cooldown state machine; this service only relays the
   request and mirrors status into the Modbus provider (mbp_heater). */
#include "i2c_backend.h"
#include "htr_i2c_map.h"

#ifndef HEATER_EXPECTED_MAP_VERSION
#define HEATER_EXPECTED_MAP_VERSION HTR_MAP_VERSION
#endif
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  state;             /* legacy 0 off,1 starting,3 running,4 cooldown (reg 55) */
    uint8_t  active_level;
    uint8_t  target_level;
    uint8_t  error;             /* vendor fault code; 0xFF = coprocessor fault w/o code */
    uint16_t supply_mv;
    uint16_t fan_rpm;
    uint16_t pump_hz_x10;
    uint16_t exchanger_raw;
    uint16_t state_seconds;
    uint16_t age_ms;
    uint16_t flags;             /* HEATER_FLAG_* (legacy reg 64 meaning) */
    uint16_t valid_frames;
    uint16_t checksum_failures;
    uint16_t transport_errors;  /* G0B1<->coprocessor I2C failures, saturating */
    /* regs 68-74 */
    uint8_t  type;              /* ACTIVE_TYPE */
    uint16_t caps;
    uint8_t  phase;             /* full generic phase 0..6 */
    uint16_t vendor_state;      /* major<<8 | sub */
    uint16_t detect;            /* DETECT_RESULT<<8 | LINE_SENSE */
    uint8_t  present;           /* coprocessor identified (DEVICE_ID + MAP_VERSION ok) */
    uint8_t  cmd_result;        /* coprocessor CMD_RESULT of the last command (HTR_RES_*) */
} heater_telemetry_t;

#define HEATER_FLAG_FRESH       (1U << 0)
#define HEATER_FLAG_COOLDOWN    (1U << 1) /* phase STOP_REQUESTED or COOLDOWN */
#define HEATER_FLAG_SAFE_OFF    (1U << 2)
#define HEATER_FLAG_COMMS_FAULT (1U << 3) /* run requested but heater link not fresh */
#define HEATER_FLAG_XPORT_FAULT (1U << 4) /* G0B1<->coprocessor I2C failed within 3 s */

#define HEATER_FLAG_MAP_MISMATCH   (1U << 5) /* identity/version incompatible, writes blocked */
#define HEATER_FLAG_MAP_UNREADABLE (1U << 6) /* identity unverified, writes blocked */

#define HEATER_POLL_MS          250U
#define HEATER_IDENT_RETRY_MS  1000U
#define HEATER_LOST_MS         5000U
#define HEATER_XPORT_STICKY_MS 3000U

/* reset_coproc may be NULL (no nRST GPIO wired). Never blocks boot. */
void    heater_init(const i2c_backend_t *bus, void (*reset_coproc)(void), uint32_t now);
void    heater_set_recovery_epoch(uint32_t (*epoch)(void));
void    heater_process(uint32_t now);
void    heater_set_request(uint8_t on);
uint8_t heater_get_request(void);
void    heater_set_level(uint8_t level);          /* 1..10, else ignored */
uint8_t heater_get_level(void);
void    heater_set_setpoint_c(int8_t c);          /* 5..30, else ignored */
int8_t  heater_get_setpoint_c(void);
void    heater_set_type_select(uint8_t type);     /* HTR_TYPE_*; forwarded once per call */
uint8_t heater_get_type_select(void);
void    heater_set_ambient_f(int16_t deg_f);      /* cabin temp for AUTOTERM regulation */
/* Queue one coprocessor COMMAND (HTR_CMD_START..HTR_CMD_AUTO_DETECT), sent
   once at the next poll. False for an unknown command. */
bool    heater_send_command(uint8_t cmd);
void    heater_get_telemetry(heater_telemetry_t *out);
#endif /* HEATER_H */
