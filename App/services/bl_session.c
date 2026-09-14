#include "bl_session.h"
#include "bl_proto.h"
#include "crc32.h"

#define PAGE_SIZE 2048u

void bl_session_init(bl_session_t *s, const iflash_backend_t *fl, bootcfg_t *cfg) {
    s->fl = fl; s->cfg = cfg;
    s->target = (cfg->active_slot == SLOT_A) ? SLOT_B : SLOT_A;
    s->target_base = bootcfg_slot_base(s->target);
    s->slot_size = SLOT_SIZE;
    s->expected_len = s->expected_crc = s->high_water = 0;
    s->state = BL_ST_IDLE; s->want_reset = 0;
}

static uint16_t erase_target(bl_session_t *s, uint8_t *resp) {
    uint32_t first = (s->target == SLOT_A) ? 16u : 128u;
    uint32_t pages = SLOT_SIZE / PAGE_SIZE;               /* 112 */
    for (uint32_t p = 0; p < pages; p++) {
        if (s->fl->erase_page(s->fl->ctx, first + p) != 0) {
            s->state = BL_ST_ERROR;
            return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_FLASH);
        }
        /* on-target: IWDG refresh happens inside the RAM-resident driver per page */
    }
    s->high_water = 0; s->state = BL_ST_ERASED;
    return bl_build_ack(resp, BL_FC_CONTROL, BL_SUB_ERASE);
}

static uint16_t handle_data(bl_session_t *s, const uint8_t *req, uint16_t len, uint8_t *resp) {
    if (s->state != BL_ST_ERASED) return bl_build_nak(resp, BL_FC_DATA, BL_ERR_STATE);
    uint32_t off; const uint8_t *d; uint16_t dl;
    if (bl_parse_data(req, len, &off, &d, &dl) != 0) return bl_build_nak(resp, BL_FC_DATA, BL_ERR_RANGE);
    if ((off & 7u) != 0u || (uint32_t)off + dl > s->slot_size) return bl_build_nak(resp, BL_FC_DATA, BL_ERR_RANGE);
    uint8_t dw[BL_CHUNK_MAX];
    uint16_t padded = (uint16_t)((dl + 7u) & ~7u);
    for (uint16_t i = 0; i < padded; i++) dw[i] = (i < dl) ? d[i] : 0xFFu;
    if (s->fl->program(s->fl->ctx, s->target_base + off, dw, padded) != 0)
        return bl_build_nak(resp, BL_FC_DATA, BL_ERR_FLASH);
    uint32_t end = off + dl; if (end > s->high_water) s->high_water = end;
    return bl_build_data_ack(resp, off);
}

static uint16_t handle_verify(bl_session_t *s, uint32_t len, uint32_t crc, uint8_t *resp) {
    if (s->state != BL_ST_ERASED || len == 0u || len > s->slot_size)
        return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_STATE);
    uint32_t c = CRC32_INIT; uint8_t buf[64];
    for (uint32_t o = 0; o < len; o += sizeof buf) {
        uint32_t n = (len - o < sizeof buf) ? (len - o) : sizeof buf;
        if (s->fl->read(s->fl->ctx, s->target_base + o, buf, n) != 0)
            return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_FLASH);
        c = crc32_update(c, buf, n);
    }
    if ((c ^ 0xFFFFFFFFu) != crc) return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_CRC);
    s->expected_len = len; s->expected_crc = crc; s->state = BL_ST_VERIFIED;
    return bl_build_ack(resp, BL_FC_CONTROL, BL_SUB_VERIFY);
}

static uint16_t handle_commit(bl_session_t *s, uint8_t *resp) {
    if (s->state != BL_ST_VERIFIED) return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_STATE);
    s->cfg->slot[s->target].state  = SLOT_STATE_TRIAL;
    s->cfg->slot[s->target].length = s->expected_len;
    s->cfg->slot[s->target].crc32  = s->expected_crc;
    s->cfg->active_slot = (uint16_t)s->target;
    s->cfg->trial_count = 0;
    if (bootcfg_save(s->fl, s->cfg) != 0) return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_FLASH);
    s->state = BL_ST_COMMITTING; s->want_reset = 1;
    return bl_build_ack(resp, BL_FC_CONTROL, BL_SUB_COMMIT);
}

uint16_t bl_session_process(bl_session_t *s, const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    if (req_len < 2u) return 0;
    if (req[1] == BL_FC_DATA) return handle_data(s, req, req_len, resp);
    if (req[1] == BL_FC_CONTROL) {
        bl_sub_t sub; uint32_t a, b;
        if (bl_parse_control(req, req_len, &sub, &a, &b) != 0)
            return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_RANGE);
        switch (sub) {
            case BL_SUB_INFO: {
                bl_info_t i = { 1u, (uint8_t)s->target, s->slot_size, BL_CHUNK_MAX, 1u };
                return bl_build_info(resp, &i);
            }
            case BL_SUB_ERASE:  return erase_target(s, resp);
            case BL_SUB_VERIFY: return handle_verify(s, a, b, resp);
            case BL_SUB_COMMIT: return handle_commit(s, resp);
            case BL_SUB_ABORT:  s->state = BL_ST_IDLE; return bl_build_ack(resp, BL_FC_CONTROL, BL_SUB_ABORT);
            case BL_SUB_STATUS: return bl_build_status(resp, (uint8_t)s->state, s->high_water);
            default:            return bl_build_nak(resp, BL_FC_CONTROL, BL_ERR_RANGE);
        }
    }
    return bl_build_nak(resp, req[1], BL_ERR_STATE);
}
