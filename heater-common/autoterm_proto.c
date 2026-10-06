#include "autoterm_proto.h"
#include "crc16_modbus.h"
#include "htr_types.h"
#include <string.h>

size_t AT_BuildFrame(uint8_t dir, uint8_t cmd, const uint8_t *payload,
                     uint8_t len, uint8_t *out)
{
    uint16_t crc;
    size_t n;
    if (len > AT_MAX_PAYLOAD) return 0u;
    out[0] = AT_PREAMBLE;
    out[1] = dir;
    out[2] = len;
    out[3] = 0x00u;
    out[4] = cmd;
    if (len > 0u) memcpy(&out[5], payload, len);
    n = 5u + len;
    crc = crc16_modbus(out, n);
    out[n] = (uint8_t)(crc >> 8);
    out[n + 1u] = (uint8_t)(crc & 0xFFu);
    return n + 2u;
}

size_t AT_BuildSimple(uint8_t cmd, uint8_t out[AT_OVERHEAD])
{
    return AT_BuildFrame(AT_DIR_REQ, cmd, NULL, 0u, out);
}

size_t AT_BuildTemp(int8_t temp_c, uint8_t out[8])
{
    uint8_t p[1];
    p[0] = (uint8_t)temp_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_TEMP, p, 1u, out);
}

/* Captured start: payload FF FF 02 1D 01 FF 11 (29 C). Only byte 3 (setpoint)
   is parameterised; the rest is copied verbatim, including the trailing 0x11
   whose meaning is not yet known. */
size_t AT_BuildStart(int8_t setpoint_c, uint8_t out[14])
{
    uint8_t p[7] = { 0xFFu, 0xFFu, 0x02u, 0x00u, 0x01u, 0xFFu, 0x11u };
    p[3] = (uint8_t)setpoint_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_START, p, 7u, out);
}

size_t AT_BuildSettingsWrite(int8_t setpoint_c, uint8_t out[13])
{
    uint8_t p[6] = { 0xFFu, 0xFFu, 0x02u, 0x00u, 0x01u, 0xFFu };
    p[3] = (uint8_t)setpoint_c;
    return AT_BuildFrame(AT_DIR_REQ, AT_CMD_SETTINGS, p, 6u, out);
}

void AT_ParserReset(at_parser_t *p)
{
    p->idx = 0u;
    p->total = 0u;
}

bool AT_ParseByte(at_parser_t *p, uint8_t byte, at_frame_t *out)
{
    uint16_t crc;
    uint8_t n;
    switch (p->idx) {
    case 0u:
        if (byte == AT_PREAMBLE) p->buf[p->idx++] = byte;
        return false;
    case 1u:
        if (byte == AT_DIR_RESP) {
            p->buf[p->idx++] = byte;
        } else if (byte != AT_PREAMBLE) {
            p->idx = 0u;               /* AA AA: stay at idx 1 */
        }
        return false;
    case 2u:
        if (byte > AT_MAX_PAYLOAD) {
            p->idx = (byte == AT_PREAMBLE) ? 1u : 0u;
            return false;
        }
        p->buf[p->idx++] = byte;
        p->total = (uint8_t)(AT_OVERHEAD + byte);
        return false;
    case 3u:
        if (byte != 0x00u) {
            p->idx = (byte == AT_PREAMBLE) ? 1u : 0u;
            return false;
        }
        p->buf[p->idx++] = byte;
        return false;
    default:
        p->buf[p->idx++] = byte;
        if (p->idx < p->total) return false;
        n = (uint8_t)(p->total - 2u);
        crc = crc16_modbus(p->buf, n);
        p->idx = 0u;
        if ((p->buf[n] != (uint8_t)(crc >> 8)) ||
            (p->buf[n + 1u] != (uint8_t)(crc & 0xFFu))) {
            p->crc_fails++;
            return false;
        }
        out->cmd = p->buf[4];
        out->len = p->buf[2];
        memcpy(out->payload, &p->buf[5], out->len);
        return true;
    }
}

bool AT_DecodeStatus(const at_frame_t *f, at_status_t *out)
{
    const uint8_t *p = f->payload;
    if ((f->cmd != AT_CMD_STATUS) || (f->len < AT_STATUS_LEN)) return false;
    out->major = p[0];
    out->minor = p[1];
    out->internal_c = (int8_t)p[3];
    out->external_c = p[4];
    out->supply_mv = (uint16_t)((uint16_t)p[6] * 100u);
    out->exch_raw = (uint16_t)(((uint16_t)p[7] << 8) | p[8]);
    out->fan_rps_req = p[11];
    out->fan_rps = p[12];
    out->pump_hz_x100_req = p[14];
    out->pump_hz_x100 = p[16];
    return true;
}

uint8_t AT_PhaseHint(uint8_t major, uint8_t minor)
{
    switch (major) {
    case 0u: return HTR_HINT_OFF;
    case 1u: return HTR_HINT_STARTING;
    case 2u: return (minor < 4u) ? HTR_HINT_STARTING : HTR_HINT_RUNNING;
    case 3u: return HTR_HINT_RUNNING;
    case 4u: return HTR_HINT_COOLDOWN;
    default: return HTR_HINT_FAULT;
    }
}
