#ifndef HTR_TYPES_H
#define HTR_TYPES_H
/* Heater-agnostic types shared by every heater driver, the coprocessor core,
   and the G0B1 client. Portable C: no MCU headers. */
#include <stdbool.h>
#include <stdint.h>

/* Heater type ids (register HEATER_TYPE / ACTIVE_TYPE). */
#define HTR_TYPE_NONE      0x00u
#define HTR_TYPE_VEVOR     0x01u
#define HTR_TYPE_AUTOTERM  0x02u
#define HTR_TYPE_AUTO      0xFFu

/* Wiring class: selects UART + TX routing. */
#define HTR_WIRING_NONE    0u
#define HTR_WIRING_3W      1u
#define HTR_WIRING_4W      2u

/* Generic phase (register PHASE). */
typedef enum {
    HTR_PHASE_OFF = 0,
    HTR_PHASE_DETECTING = 1,
    HTR_PHASE_STARTING = 2,
    HTR_PHASE_RUNNING = 3,
    HTR_PHASE_STOP_REQUESTED = 4,
    HTR_PHASE_COOLDOWN = 5,
    HTR_PHASE_FAULT = 6
} htr_phase_t;

/* What a driver believes the heater itself is doing, from its last frame. */
typedef enum {
    HTR_HINT_UNKNOWN = 0,
    HTR_HINT_OFF,
    HTR_HINT_STARTING,
    HTR_HINT_RUNNING,
    HTR_HINT_COOLDOWN,
    HTR_HINT_FAULT
} htr_hint_t;

typedef enum { HTR_PROBE_BUSY = 0, HTR_PROBE_FOUND, HTR_PROBE_ABSENT } htr_probe_t;

/* Driver capabilities (register CAPS). */
#define HTR_CAP_LEVEL       (1u << 0)
#define HTR_CAP_SETPOINT    (1u << 1)
#define HTR_CAP_AMBIENT_IN  (1u << 2)
#define HTR_CAP_FAN_RPM     (1u << 3)
#define HTR_CAP_PUMP        (1u << 4)
#define HTR_CAP_SUPPLY_V    (1u << 5)
#define HTR_CAP_EXCH_TEMP   (1u << 6)
#define HTR_CAP_FAULT_CODE  (1u << 7)

/* Field-valid bits (register VALID): a clear bit means "not decoded". */
#define HTR_V_FAULT_CODE    (1u << 0)
#define HTR_V_ACTIVE_LEVEL  (1u << 1)
#define HTR_V_SUPPLY_MV     (1u << 2)
#define HTR_V_FAN_RPM       (1u << 3)
#define HTR_V_PUMP_HZ       (1u << 4)
#define HTR_V_EXCH_RAW      (1u << 5)
#define HTR_V_EXCH_C        (1u << 6)
#define HTR_V_STATE_SECONDS (1u << 7)

#define HTR_AMBIENT_UNKNOWN ((int16_t)0x7FFF)

typedef struct {
    uint8_t run;            /* 0/1 */
    uint8_t level;          /* 1..10, CAP_LEVEL drivers */
    int8_t  setpoint_c;     /* CAP_SETPOINT drivers */
    int16_t ambient_c_x10;  /* CAP_AMBIENT_IN drivers; HTR_AMBIENT_UNKNOWN if none */
} htr_request_t;

typedef struct {
    uint8_t  hint;          /* htr_hint_t */
    uint8_t  vendor_state;
    uint8_t  vendor_sub;
    uint8_t  fault_code;
    uint8_t  active_level;
    uint16_t supply_mv;
    uint16_t fan_rpm;
    uint16_t pump_hz_x10;
    uint16_t exch_raw;
    int16_t  exch_c_x10;
    uint16_t state_seconds;
    uint16_t valid;         /* HTR_V_* */
    bool     any_rx;        /* at least one valid frame since init */
    uint32_t last_rx_tick;
    uint32_t valid_frames;
    uint32_t crc_fails;
    uint32_t transport_errs;
} htr_status_t;

#endif /* HTR_TYPES_H */
