#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"
#include "modbus_frame.h"

static uint16_t s_counter[MB_COUNTER_COUNT];
static bool     s_test_mode;

void mb_engine_init(void) {
    for (uint8_t i = 0; i < MB_COUNTER_COUNT; i++) s_counter[i] = 0;
    s_test_mode = false;
}
uint16_t mb_engine_counter(uint8_t idx) { return (idx < MB_COUNTER_COUNT) ? s_counter[idx] : 0; }
bool     mb_engine_test_mode(void) { return s_test_mode; }

/* Finalize an exception response into resp; returns its length. */
static uint16_t make_exception(uint8_t *resp, uint8_t fc, modbus_exc_t exc) {
    resp[MB_F_ADDR] = MB_SLAVE_ADDR;
    resp[MB_F_FUNCTION] = (uint8_t)(fc | MB_ERROR_RESPONSE);
    resp[2] = (uint8_t)exc;
    return 3u;
}

/* Append CRC (lo,hi) after `len` bytes; return total length. */
static uint16_t finalize(uint8_t *resp, uint16_t len) {
    uint16_t crc = modbus_crc16(resp, len);
    resp[len] = (uint8_t)crc;
    resp[len + 1] = (uint8_t)(crc >> 8);
    return (uint16_t)(len + 2u);
}

/* FC 0x03 / 0x04: read `count` regs starting at 1-based (start+1). */
static uint16_t handle_read(const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    uint8_t  fc    = req[MB_F_FUNCTION];

    if (req_len < MB_HEADER_SIZE + 2u)
        return finalize(resp, make_exception(resp, fc, MB_EXC_ILLEGAL_VALUE));

    uint16_t start = (uint16_t)(req[MB_F_START_HI] << 8) | req[MB_F_START_LO];
    uint16_t count = (uint16_t)(req[MB_F_QTY_HI] << 8) | req[MB_F_QTY_LO];

    if (count < 1u || count > 0x7Du) return finalize(resp, make_exception(resp, fc, MB_EXC_ILLEGAL_VALUE));
    if ((uint32_t)start + count > MB_REG_LIMIT) return finalize(resp, make_exception(resp, fc, MB_EXC_ILLEGAL_ADDRESS));

    uint16_t idx = MB_F_FUNCTION + 1u;
    resp[MB_F_ADDR] = MB_SLAVE_ADDR;
    resp[MB_F_FUNCTION] = fc;
    resp[idx++] = (uint8_t)(count * 2u);
    for (uint16_t i = 0; i < count; i++) {
        uint16_t val = 0;
        modbus_exc_t exc = mb_reg_read((uint16_t)(start + i + 1u), &val);
        if (exc != MB_EXC_NONE) return finalize(resp, make_exception(resp, fc, exc));
        resp[idx++] = (uint8_t)(val >> 8);
        resp[idx++] = (uint8_t)val;
    }
    return finalize(resp, idx);
}

/* FC 0x06: write single register at 1-based (start+1) from value bytes [4][5]. */
static uint16_t handle_write_single(const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    if (req_len < MB_HEADER_SIZE + 2u)
        return finalize(resp, make_exception(resp, MB_FC_WRITE_SINGLE, MB_EXC_ILLEGAL_VALUE));

    uint16_t start = (uint16_t)(req[MB_F_START_HI] << 8) | req[MB_F_START_LO];
    uint16_t val   = (uint16_t)(req[MB_F_QTY_HI] << 8) | req[MB_F_QTY_LO]; /* value in bytes 4,5 */
    modbus_exc_t exc = mb_reg_write((uint16_t)(start + 1u), val);
    if (exc != MB_EXC_NONE) return finalize(resp, make_exception(resp, MB_FC_WRITE_SINGLE, exc));
    for (uint16_t i = 0; i < MB_HEADER_SIZE; i++) resp[i] = req[i];   /* echo request */
    return finalize(resp, MB_HEADER_SIZE);
}

/* FC 0x10: write multiple registers starting at 1-based (start+1). */
static uint16_t handle_write_multiple(const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    if (req_len < MB_HEADER_SIZE)   /* need offsets 0..5 (addr..qty_lo) present */
        return finalize(resp, make_exception(resp, MB_FC_WRITE_MULTIPLE, MB_EXC_ILLEGAL_VALUE));

    uint16_t start = (uint16_t)(req[MB_F_START_HI] << 8) | req[MB_F_START_LO];
    uint16_t count = (uint16_t)(req[MB_F_QTY_HI] << 8) | req[MB_F_QTY_LO];
    if (count < 1u || count > 0x7Du) return finalize(resp, make_exception(resp, MB_FC_WRITE_MULTIPLE, MB_EXC_ILLEGAL_VALUE));
    if ((uint32_t)start + count > MB_REG_LIMIT) return finalize(resp, make_exception(resp, MB_FC_WRITE_MULTIPLE, MB_EXC_ILLEGAL_ADDRESS));

    if ((uint32_t)req_len < (uint32_t)(MB_HEADER_SIZE + 1u) + 2u * count + 2u)
        return finalize(resp, make_exception(resp, MB_FC_WRITE_MULTIPLE, MB_EXC_ILLEGAL_VALUE));

    uint16_t ndx = MB_HEADER_SIZE + 1u;    /* skip byte-count at [6]; values start at [7] */
    for (uint16_t i = 0; i < count; i++) {
        uint16_t val = (uint16_t)(req[ndx] << 8) | req[ndx + 1u];
        ndx += 2u;
        modbus_exc_t exc = mb_reg_write((uint16_t)(start + i + 1u), val);
        if (exc != MB_EXC_NONE) return finalize(resp, make_exception(resp, MB_FC_WRITE_MULTIPLE, exc));
    }
    resp[MB_F_ADDR] = MB_SLAVE_ADDR;
    resp[MB_F_FUNCTION] = MB_FC_WRITE_MULTIPLE;
    resp[MB_F_START_HI] = req[MB_F_START_HI]; resp[MB_F_START_LO] = req[MB_F_START_LO];
    resp[MB_F_QTY_HI]   = req[MB_F_QTY_HI];   resp[MB_F_QTY_LO]   = req[MB_F_QTY_LO];
    return finalize(resp, MB_HEADER_SIZE);
}

/* Returns response length (pre-CRC handlers call finalize themselves), or 0 for no response. */
static uint16_t dispatch_fc(const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    switch (req[MB_F_FUNCTION]) {
        case MB_FC_READ_HOLDING:
        case MB_FC_READ_INPUT:
            return handle_read(req, req_len, resp);
        case MB_FC_WRITE_SINGLE:
            return handle_write_single(req, req_len, resp);
        case MB_FC_WRITE_MULTIPLE:
            return handle_write_multiple(req, req_len, resp);
        default:
            return finalize(resp, make_exception(resp, req[MB_F_FUNCTION], MB_EXC_ILLEGAL_FUNCTION));
    }
}

void mb_engine_process(const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t *resp_len) {
    *resp_len = 0;
    mb_frame_status_t st = mb_check_frame(req, req_len, MB_SLAVE_ADDR);
    if (st != MB_FRAME_OK) return;                 /* not-for-us / short / bad CRC -> silent */
    if (req[MB_F_ADDR] == MB_BROADCAST_ADDR) {     /* broadcast: act, no response (no read/broadcast here) */
        return;
    }
    *resp_len = dispatch_fc(req, req_len, resp);
}
