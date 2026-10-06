#ifndef VEVOR_PROTO_H
#define VEVOR_PROTO_H

#include "htr_uart.h"
#include <stdbool.h>
#include <stdint.h>

#define VEVOR_CONTROLLER_FRAME_SIZE 16U
#define VEVOR_HEATER_FRAME_SIZE     56U

typedef enum {
    VEVOR_REPORTED_OFF = 0,
    VEVOR_REPORTED_PREHEAT = 1,
    VEVOR_REPORTED_IGNITION = 2,
    VEVOR_REPORTED_RUNNING = 3,
    VEVOR_REPORTED_COOLDOWN = 4
} VEVOR_ReportedState_t;

typedef enum {
    VEVOR_REQUEST_STOP = 0,
    VEVOR_REQUEST_START,
    VEVOR_REQUEST_RUN
} VEVOR_RequestMode_t;

typedef struct {
    bool valid;
    uint8_t state;
    uint8_t level;
    uint8_t error;
    uint16_t supply_mv;
    bool cooldown_flag;
    uint16_t exchanger_raw;
    uint16_t state_seconds;
    uint16_t pump_hz_x10;
    uint16_t fan_rpm;
    uint32_t received_tick;
} VEVOR_Status_t;

typedef struct {
    const htr_uart_t *uart;
    VEVOR_RequestMode_t request_mode;
    uint8_t requested_level;
    uint8_t start_frames_remaining;
    uint8_t rx_frame[VEVOR_HEATER_FRAME_SIZE];
    uint8_t rx_index;
    uint32_t last_tx_tick;
    uint32_t last_rx_poll_tick;
    uint32_t valid_frames;
    uint32_t checksum_failures;
    uint32_t transport_errors;
    VEVOR_Status_t status;
} VEVOR_t;

bool VEVOR_Init(VEVOR_t *heater, const htr_uart_t *uart, uint32_t now);
void VEVOR_Process(VEVOR_t *heater, uint32_t now);
bool VEVOR_Start(VEVOR_t *heater, uint8_t level);
bool VEVOR_SetLevel(VEVOR_t *heater, uint8_t level);
void VEVOR_Stop(VEVOR_t *heater);
bool VEVOR_IsResponseFresh(const VEVOR_t *heater, uint32_t now);
bool VEVOR_IsSafeToPowerDown(const VEVOR_t *heater, uint32_t now);

/* Exposed for host tests and comparison with captured frames. */
void VEVOR_BuildStartFrame(uint8_t level, uint8_t out[16]);
void VEVOR_BuildRunFrame(uint8_t level, uint8_t out[16]);
void VEVOR_BuildStopFrame(uint8_t level, uint8_t out[16]);
uint8_t VEVOR_Checksum(const uint8_t *frame, uint8_t checksum_index);

#endif
