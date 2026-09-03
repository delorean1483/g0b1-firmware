#include "bootcfg.h"
#include "crc32.h"
#include <string.h>
#include <stddef.h>

#define CFG_BASE       0x08000000u
#define PAGE_SIZE      2048u
#define CRC_OFF        (offsetof(bootcfg_t, crc32))

_Static_assert(sizeof(bootcfg_t) % 8u == 0u, "dword");

uint32_t bootcfg_slot_base(slot_id_t s) { return (s == SLOT_A) ? SLOT_A_BASE : SLOT_B_BASE; }

static uint32_t page_addr(uint32_t page) { return CFG_BASE + page * PAGE_SIZE; }

static int record_valid(const bootcfg_t *r) {
    if (r->magic != BOOTCFG_MAGIC || r->version != BOOTCFG_VERSION) return 0;
    uint32_t want = crc32_compute((const uint8_t *)r, CRC_OFF);
    return (want == r->crc32);
}

static int read_page(const iflash_backend_t *fl, uint32_t page, bootcfg_t *out) {
    return fl->read(fl->ctx, page_addr(page), (uint8_t *)out, sizeof *out);
}

int bootcfg_load(const iflash_backend_t *fl, bootcfg_t *out) {
    bootcfg_t a, b; int va = 0, vb = 0;
    if (read_page(fl, BOOTCFG_PAGE_0, &a) == 0) va = record_valid(&a);
    if (read_page(fl, BOOTCFG_PAGE_1, &b) == 0) vb = record_valid(&b);
    if (va && vb) { *out = (a.seq >= b.seq) ? a : b; return 0; }
    if (va) { *out = a; return 0; }
    if (vb) { *out = b; return 0; }
    memset(out, 0, sizeof *out);
    return -1;
}

int bootcfg_save(const iflash_backend_t *fl, bootcfg_t *cfg) {
    /* Write to the page NOT holding the current newest record (ping-pong). */
    bootcfg_t a, b; int va = 0, vb = 0;
    if (read_page(fl, BOOTCFG_PAGE_0, &a) == 0) va = record_valid(&a);
    if (read_page(fl, BOOTCFG_PAGE_1, &b) == 0) vb = record_valid(&b);
    /* Default (neither page holds a valid record: fresh device) seeds page 0 first. */
    uint32_t target = BOOTCFG_PAGE_0;
    if (va && vb)      target = (a.seq >= b.seq) ? BOOTCFG_PAGE_1 : BOOTCFG_PAGE_0;
    else if (va)       target = BOOTCFG_PAGE_1;
    else if (vb)       target = BOOTCFG_PAGE_0;

    cfg->magic = BOOTCFG_MAGIC; cfg->version = BOOTCFG_VERSION; cfg->_pad = 0; cfg->_pad2 = 0;
    cfg->seq += 1u;
    cfg->crc32 = crc32_compute((const uint8_t *)cfg, CRC_OFF);

    if (fl->erase_page(fl->ctx, target) != 0) return -1;
    /* program in 8-byte dwords; sizeof(bootcfg_t) is a multiple of 8 by design. */
    return fl->program(fl->ctx, page_addr(target), (const uint8_t *)cfg, sizeof *cfg);
}

int bootcfg_slot_crc_ok(const iflash_backend_t *fl, const bootcfg_t *cfg, slot_id_t s) {
    if (cfg->slot[s].state == SLOT_STATE_BAD) return 0;
    uint32_t len = cfg->slot[s].length;
    if (len == 0u || len > SLOT_SIZE) return 0;
    uint32_t base = bootcfg_slot_base(s);
    uint32_t crc = CRC32_INIT;
    uint8_t buf[64];
    for (uint32_t off = 0; off < len; off += sizeof buf) {
        uint32_t n = (len - off < sizeof buf) ? (len - off) : sizeof buf;
        if (fl->read(fl->ctx, base + off, buf, n) != 0) return 0;
        crc = crc32_update(crc, buf, n);
    }
    return ((crc ^ 0xFFFFFFFFu) == cfg->slot[s].crc32) ? 1 : 0;
}
