#include "heater.h"
#include <string.h>

/* WK2132 DIP-switch wiring for the heater's I2C-UART bridge: channel 0,
   IA1/IA0 both strapped low (base address 0x10). Confirm against the board
   schematic during bench bring-up (Task 6) -- the fake backend used by the
   host tests below does not model per-address state, so this choice does
   not affect test behavior. */
#define HEATER_WK2132_CHANNEL 0U
#define HEATER_WK2132_IA1     false
#define HEATER_WK2132_IA0     false

static WK2132_t            s_wk;
static VEVOR_t              s_vevor;
static uint8_t              s_target_level = 1U;
static uint8_t              s_request = 0U;
static heater_telemetry_t   s_telemetry;
static uint32_t             s_prev_transport_errors;

static uint16_t saturate_u16(uint32_t value)
{
    return (value > 0xFFFFU) ? 0xFFFFU : (uint16_t)value;
}

void heater_init(const wk2132_i2c_t *bus, uint32_t now)
{
    (void)WK2132_Init(&s_wk, bus, HEATER_WK2132_CHANNEL,
                      HEATER_WK2132_IA1, HEATER_WK2132_IA0);
    (void)WK2132_Begin4800_8N1(&s_wk);
    (void)WK2132_FlushRx(&s_wk);
    (void)VEVOR_Init(&s_vevor, &s_wk, now);

    s_target_level = 1U;
    s_request = 0U;
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_prev_transport_errors = 0U;
}

void heater_process(uint32_t now)
{
    uint16_t flags = 0U;
    bool fresh;
    bool safe_off;

    VEVOR_Process(&s_vevor, now);

    fresh = VEVOR_IsResponseFresh(&s_vevor, now);
    safe_off = VEVOR_IsSafeToPowerDown(&s_vevor, now);

    if (fresh) {
        flags |= HEATER_FLAG_FRESH;
    }
    if (s_vevor.status.cooldown_flag) {
        flags |= HEATER_FLAG_COOLDOWN;
    }
    if (safe_off) {
        flags |= HEATER_FLAG_SAFE_OFF;
    }
    /* A lost link while commanded ON is a fault, never safe-off. */
    if ((s_request == 1U) && !fresh) {
        flags |= HEATER_FLAG_COMMS_FAULT;
    }
    if (s_vevor.transport_errors > s_prev_transport_errors) {
        flags |= HEATER_FLAG_XPORT_FAULT;
    }
    s_prev_transport_errors = s_vevor.transport_errors;

    s_telemetry.state            = s_vevor.status.state;
    s_telemetry.active_level     = s_vevor.status.level;
    s_telemetry.target_level     = s_target_level;
    s_telemetry.error            = s_vevor.status.error;
    s_telemetry.supply_mv        = s_vevor.status.supply_mv;
    s_telemetry.fan_rpm          = s_vevor.status.fan_rpm;
    s_telemetry.pump_hz_x10      = s_vevor.status.pump_hz_x10;
    s_telemetry.exchanger_raw    = s_vevor.status.exchanger_raw;
    s_telemetry.state_seconds    = s_vevor.status.state_seconds;
    s_telemetry.age_ms           = saturate_u16(
        (uint32_t)(now - s_vevor.status.received_tick));
    s_telemetry.flags            = flags;
    s_telemetry.valid_frames     = (uint16_t)(s_vevor.valid_frames & 0xFFFFU);
    s_telemetry.checksum_failures = saturate_u16(s_vevor.checksum_failures);
    s_telemetry.transport_errors  = saturate_u16(s_vevor.transport_errors);
}

void heater_set_request(uint8_t on)
{
    s_request = (on != 0U) ? 1U : 0U;
    if (s_request == 1U) {
        (void)VEVOR_Start(&s_vevor, s_target_level);
    } else {
        VEVOR_Stop(&s_vevor);
    }
}

uint8_t heater_get_request(void)
{
    return s_request;
}

void heater_set_level(uint8_t level)
{
    if ((level < 1U) || (level > 10U)) {
        return;
    }
    s_target_level = level;
    if (s_request == 1U) {
        (void)VEVOR_SetLevel(&s_vevor, level);
    }
}

uint8_t heater_get_level(void)
{
    return s_target_level;
}

void heater_get_telemetry(heater_telemetry_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_telemetry;
}
