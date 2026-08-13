#include "mb_engine.h"
#include "mb_regmodel.h"
#include "modbus_crc.h"
#include "modbus_frame.h"
#include <string.h>

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

static const char MB_SLAVE_ID[] = "EF-G0B1R";

static uint16_t handle_read_exception(uint8_t *resp) {
    resp[MB_F_ADDR] = MB_SLAVE_ADDR;
    resp[MB_F_FUNCTION] = MB_FC_READ_EXCEPTION;
    resp[2] = 0x00u;
    return finalize(resp, 3u);
}

static uint16_t handle_report_slave_id(uint8_t *resp) {
    uint8_t n = (uint8_t)strlen(MB_SLAVE_ID);
    resp[MB_F_ADDR] = MB_SLAVE_ADDR;
    resp[MB_F_FUNCTION] = MB_FC_REPORT_SLAVE_ID;
    resp[2] = n;                                   /* byte count */
    for (uint8_t i = 0; i < n; i++) resp[3u + i] = (uint8_t)MB_SLAVE_ID[i];
    return finalize(resp, (uint16_t)(3u + n));
}

static void bump(uint8_t idx) { if (idx < MB_COUNTER_COUNT && s_counter[idx] != 0xFFFFu) s_counter[idx]++; }

static uint16_t diag_echo(const uint8_t *req, uint8_t *resp) {
    for (uint16_t i = 0; i < MB_HEADER_SIZE; i++) resp[i] = req[i];
    return finalize(resp, MB_HEADER_SIZE);
}

static uint16_t handle_diagnostics(const uint8_t *req, uint8_t *resp) {
    uint16_t sub = (uint16_t)(req[MB_F_START_HI] << 8) | req[MB_F_START_LO];
    if (sub == MB_DIAG_CLEAR_COUNTERS) {
        for (uint8_t i = 0; i < MB_COUNTER_COUNT; i++) s_counter[i] = 0;
        return diag_echo(req, resp);
    }
    if (sub >= MB_DIAG_FIRST_COUNTER && sub <= MB_DIAG_LAST_COUNTER) {
        uint16_t v = s_counter[sub - MB_DIAG_FIRST_COUNTER];
        resp[MB_F_ADDR] = MB_SLAVE_ADDR; resp[MB_F_FUNCTION] = MB_FC_DIAGNOSTICS;
        resp[MB_F_START_HI] = req[MB_F_START_HI]; resp[MB_F_START_LO] = req[MB_F_START_LO];
        resp[MB_F_QTY_HI] = (uint8_t)(v >> 8); resp[MB_F_QTY_LO] = (uint8_t)v;
        return finalize(resp, MB_HEADER_SIZE);
    }
    if (sub == MB_DIAG_ENTER_TEST) s_test_mode = true;
    return diag_echo(req, resp);   /* query-data / restart / clr-overrun / enter-test / other */
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
        case MB_FC_READ_EXCEPTION:
            return handle_read_exception(resp);
        case MB_FC_REPORT_SLAVE_ID:
            return handle_report_slave_id(resp);
        case MB_FC_DIAGNOSTICS:
            return handle_diagnostics(req, resp);
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
    /* TODO: Task 6 will formalize counter policy across all frame-flow paths. */
    bump(0);   /* interim: bus-message counter */
    *resp_len = dispatch_fc(req, req_len, resp);
}
