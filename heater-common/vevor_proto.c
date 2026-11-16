#include "vevor_proto.h"
#include <string.h>

#define VEVOR_TX_INTERVAL_MS       1000UL
#define VEVOR_RX_POLL_INTERVAL_MS    10UL
#define VEVOR_RESPONSE_FRESH_MS    3000UL
#define VEVOR_START_FRAME_COUNT       4U
/* A frame takes ~117 ms at 4800 baud, its bytes back to back; 30 ms with no
   byte mid-frame (~14 byte times) means it was cut short. */
#define VEVOR_RX_GAP_MS              30UL
/* Supply byte [11] is in 0.1 V: 0 in standby, else a real battery. Below
   8.0 V is a corrupted frame (bench: "6.4 V" from an oscillating RX). */
#define VEVOR_SUPPLY_MIN_DV          80U

uint8_t VEVOR_Checksum(const uint8_t *frame, uint8_t checksum_index)
{
    uint16_t sum = 0U;
    uint8_t i;
    for (i = 2U; i < checksum_index; ++i) sum += frame[i];
    return (uint8_t)sum;
}

static void build_frame(uint8_t command, uint8_t level, uint8_t state,
                        uint8_t qualifier, uint8_t out[16])
{
    static const uint8_t blank[16] = {
        0xAA, 0x66, 0, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    memcpy(out, blank, sizeof(blank));
    out[2] = command;
    out[8] = level;
    out[9] = state;
    out[10] = qualifier;
    out[15] = VEVOR_Checksum(out, 15U);
}

void VEVOR_BuildStartFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x06U, level, 0x06U, 0x00U, out);
}

void VEVOR_BuildRunFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x02U, level, 0x06U, 0x02U, out);
}

void VEVOR_BuildStopFrame(uint8_t level, uint8_t out[16])
{
    build_frame(0x06U, level, 0x05U, 0x00U, out);
}

static bool decode_response(VEVOR_t *heater, const uint8_t *frame,
                            uint32_t now)
{
    if ((frame[0] != 0xAAU) || (frame[1] != 0x77U) ||
        (VEVOR_Checksum(frame, 55U) != frame[55])) {
        heater->checksum_failures++;
        return false;
    }
    /* The 8-bit sum let corrupted frames through on the bench: also require
       fields a heater can report (state 0..4, level 0..10, supply 0 or
       >= 8.0 V). Every frame in the bench captures passes. */
    if ((frame[5] > (uint8_t)VEVOR_REPORTED_COOLDOWN) || (frame[6] > 10U) ||
        ((frame[11] != 0U) && (frame[11] < VEVOR_SUPPLY_MIN_DV))) {
        heater->checksum_failures++;
        return false;
    }
    heater->status.state = frame[5];
    heater->status.level = frame[6];
    heater->status.error = frame[7];
    heater->status.supply_mv = (uint16_t)frame[11] * 100U;
    heater->status.cooldown_flag = (frame[14] != 0U);
    heater->status.exchanger_raw = (uint16_t)(((uint16_t)frame[16] << 8) |
                                              frame[17]);
    heater->status.state_seconds = (uint16_t)(((uint16_t)frame[20] << 8) |
                                              frame[21]);
    heater->status.pump_hz_x10 = frame[23];
    heater->status.fan_rpm = (uint16_t)(((uint16_t)frame[28] << 8) |
                                       frame[29]);
    heater->status.received_tick = now;
    heater->status.valid = true;
    heater->valid_frames++;
    return true;
}

static void parse_byte(VEVOR_t *heater, uint8_t byte, uint32_t now)
{
    if (heater->rx_index == 0U) {
        if (byte == 0xAAU) heater->rx_frame[heater->rx_index++] = byte;
        return;
    }
    if (heater->rx_index == 1U) {
        if (byte == 0x77U) {
            heater->rx_frame[heater->rx_index++] = byte;
        } else if (byte == 0xAAU) {
            heater->rx_frame[0] = byte;
        } else {
            heater->rx_index = 0U;
        }
        return;
    }
    heater->rx_frame[heater->rx_index++] = byte;
    if (heater->rx_index == VEVOR_HEATER_FRAME_SIZE) {
        (void)decode_response(heater, heater->rx_frame, now);
        heater->rx_index = 0U;
    }
}

bool VEVOR_Init(VEVOR_t *heater, const htr_uart_t *uart, uint32_t now)
{
    if ((heater == NULL) || (uart == NULL)) return false;
    memset(heater, 0, sizeof(*heater));
    heater->uart = uart;
    heater->requested_level = 1U;
    heater->request_mode = VEVOR_REQUEST_STOP;
    /* Arrange for an immediate conservative STOP poll. */
    heater->last_tx_tick = now - VEVOR_TX_INTERVAL_MS;
    heater->last_rx_poll_tick = now;
    heater->last_rx_byte_tick = now;
    return true;
}

bool VEVOR_Start(VEVOR_t *heater, uint8_t level)
{
    if ((heater == NULL) || (level < 1U) || (level > 10U)) return false;
    heater->requested_level = level;
    heater->request_mode = VEVOR_REQUEST_START;
    heater->start_frames_remaining = VEVOR_START_FRAME_COUNT;
    return true;
}

bool VEVOR_SetLevel(VEVOR_t *heater, uint8_t level)
{
    if ((heater == NULL) || (level < 1U) || (level > 10U)) return false;
    heater->requested_level = level;
    return true;
}

void VEVOR_Stop(VEVOR_t *heater)
{
    if (heater == NULL) return;
    heater->request_mode = VEVOR_REQUEST_STOP;
    heater->start_frames_remaining = 0U;
}

bool VEVOR_IsResponseFresh(const VEVOR_t *heater, uint32_t now)
{
    return (heater != NULL) && heater->status.valid &&
           ((uint32_t)(now - heater->status.received_tick) <=
            VEVOR_RESPONSE_FRESH_MS);
}

bool VEVOR_IsSafeToPowerDown(const VEVOR_t *heater, uint32_t now)
{
    return VEVOR_IsResponseFresh(heater, now) &&
           (heater->status.state == VEVOR_REPORTED_OFF);
}

void VEVOR_Process(VEVOR_t *heater, uint32_t now)
{
    uint8_t data[64];
    uint8_t tx[VEVOR_CONTROLLER_FRAME_SIZE];
    size_t count;
    size_t i;
    if ((heater == NULL) || (heater->uart == NULL)) return;

    if ((uint32_t)(now - heater->last_rx_poll_tick) >=
        VEVOR_RX_POLL_INTERVAL_MS) {
        heater->last_rx_poll_tick = now;
        count = heater->uart->read(heater->uart->ctx, data, sizeof(data));
        for (i = 0U; i < count; ++i) parse_byte(heater, data[i], now);
        if (count != 0U) {
            heater->last_rx_byte_tick = now;
        } else if ((heater->rx_index != 0U) &&
                   ((uint32_t)(now - heater->last_rx_byte_tick) >= VEVOR_RX_GAP_MS)) {
            /* Frame cut short: drop it and resync on the next AA 77, instead
               of letting it swallow the start of the next frame. */
            if (heater->rx_index >= 2U) heater->checksum_failures++;
            heater->rx_index = 0U;
        }
    }

    if ((uint32_t)(now - heater->last_tx_tick) < VEVOR_TX_INTERVAL_MS) return;
    heater->last_tx_tick = now;

    if (heater->request_mode == VEVOR_REQUEST_START) {
        VEVOR_BuildStartFrame(heater->requested_level, tx);
    } else if (heater->request_mode == VEVOR_REQUEST_RUN) {
        VEVOR_BuildRunFrame(heater->requested_level, tx);
    } else {
        VEVOR_BuildStopFrame(heater->requested_level, tx);
    }

    if (heater->uart->write(heater->uart->ctx, tx, sizeof(tx)) != sizeof(tx)) {
        heater->transport_errors++;
        return;
    }

    if (heater->request_mode == VEVOR_REQUEST_START) {
        if (heater->start_frames_remaining > 0U) {
            heater->start_frames_remaining--;
        }
        if (heater->start_frames_remaining == 0U) {
            heater->request_mode = VEVOR_REQUEST_RUN;
        }
    }
}
