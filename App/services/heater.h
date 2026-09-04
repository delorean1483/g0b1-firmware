#ifndef HEATER_H
#define HEATER_H

#include "vevor_heater.h"
#include "wk2132.h"
#include <stdint.h>

/* Telemetry snapshot for the VEVOR diesel heater. Refreshed by
   heater_process(); read out at any time via heater_get_telemetry(). */
typedef struct {
    uint8_t  state;             /* 0 off,1 preheat,2 ignition,3 running,4 cooldown */
    uint8_t  active_level;      /* heater-reported */
    uint8_t  target_level;      /* commanded 1..10 */
    uint8_t  error;
    uint16_t supply_mv;
    uint16_t fan_rpm;
    uint16_t pump_hz_x10;
    uint16_t exchanger_raw;
    uint16_t state_seconds;
    uint16_t age_ms;             /* since last valid frame, saturating 65535 */
    uint16_t flags;              /* HEATER_FLAG_* below */
    uint16_t valid_frames;       /* low 16 bits of the running count */
    uint16_t checksum_failures;  /* saturating */
    uint16_t transport_errors;   /* saturating */
} heater_telemetry_t;

#define HEATER_FLAG_FRESH       (1U << 0) /* last frame within the 3 s freshness window */
#define HEATER_FLAG_COOLDOWN    (1U << 1) /* heater reports it is in cooldown */
#define HEATER_FLAG_SAFE_OFF    (1U << 2) /* fresh AND reported state == off */
#define HEATER_FLAG_COMMS_FAULT (1U << 3) /* commanded ON but link is not fresh */
#define HEATER_FLAG_XPORT_FAULT (1U << 4) /* transport_errors increased since last snapshot */

/* WK2132_Init + WK2132_Begin4800_8N1 + WK2132_FlushRx + VEVOR_Init. */
void    heater_init(const wk2132_i2c_t *bus, uint32_t now);
/* VEVOR_Process(now) then recompute the telemetry snapshot + safety flags. */
void    heater_process(uint32_t now);
/* 1 -> VEVOR_Start(target_level); 0 -> stop request (FSM keeps sending stop
   frames through cooldown -- off is never instantaneous). */
void    heater_set_request(uint8_t on);
uint8_t heater_get_request(void);
/* 1..10; out of range is ignored. Propagated live via VEVOR_SetLevel while
   a start/run request is outstanding. */
void    heater_set_level(uint8_t level);
uint8_t heater_get_level(void);
void    heater_get_telemetry(heater_telemetry_t *out);

#endif /* HEATER_H */
