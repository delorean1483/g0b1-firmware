#include "nvm.h"
#include "nvm_map.h"
#include "nvm_record.h"
#include "nvm_defaults.h"

static const nvm_backend_t *s_be;
static uint8_t  s_shadow[NVM_PARAM_SIZE];
static bool     s_dirty;
static uint32_t s_seq;         /* sequence of the latest committed record */
static uint32_t s_next_slot;   /* slot index to write the next record into */
static uint32_t s_slots_per_sector;
static uint32_t s_slots_total;

static uint32_t slot_addr(uint32_t slot) {
    uint32_t sec = slot / s_slots_per_sector;
    uint32_t off = (slot % s_slots_per_sector) * NVM_RECORD_SIZE;
    return sec * s_be->sector_size + off;
}

void nvm_init(const nvm_backend_t *be) {
    s_be = be;
    s_slots_per_sector = be->sector_size / NVM_RECORD_SIZE;
    s_slots_total      = be->sector_count * s_slots_per_sector;

    /* Guard against div-by-zero and enforce >=2-sector invariant. */
    if (s_slots_per_sector == 0u || be->sector_count < 2u) {
        nvm_apply_factory_defaults(s_shadow);
        s_seq = 0u; s_next_slot = 0u; s_dirty = false;   /* RAM-only: commit() becomes a no-op */
        return;
    }

    uint32_t best_seq = 0, tmp_seq;
    bool found = false;
    uint32_t best_slot = 0;
    uint8_t payload[NVM_PARAM_SIZE];
    uint8_t best_payload[NVM_PARAM_SIZE];

    /* Bounded tail scan. The journal is an append-only circular log: every lap
       starts at slot 0 and writes strictly-increasing seq numbers, erasing each
       sector just before reusing it. So reading forward from slot 0 the seq rises
       until the newest record (the tail); the next slot is then either erased
       (blank tail) or holds an older lap's record (seq drops). Stop at that
       boundary — scan cost is O(live records), not O(whole device). A full sweep
       of all s_slots_total records took ~50 s on the real 8 MB NOR and tripped
       the boot watchdog. A torn last write leaves an invalid record at the tail,
       so the read simply fails there and best_* still holds the prior good
       record — identical recovery to the old full sweep. */
    for (uint32_t slot = 0; slot < s_slots_total; slot++) {
        if (!nvm_record_read(be, slot_addr(slot), &tmp_seq, payload))
            break;                       /* erased/invalid: end of the live region */
        if (found && tmp_seq <= best_seq)
            break;                       /* seq stopped rising: wrapped into an older lap */
        found = true; best_seq = tmp_seq; best_slot = slot;
        for (uint32_t i = 0; i < NVM_PARAM_SIZE; i++) best_payload[i] = payload[i];
    }

    if (found) {
        for (uint32_t i = 0; i < NVM_PARAM_SIZE; i++) s_shadow[i] = best_payload[i];
        s_seq = best_seq;
        s_next_slot = (best_slot + 1u) % s_slots_total;
        s_dirty = false;
    } else {
        nvm_apply_factory_defaults(s_shadow);
        s_seq = 0; s_next_slot = 0; s_dirty = true;
        (void)nvm_commit();     /* write the first record (seq 1) */
    }
}

uint8_t nvm_read_byte(uint16_t addr) {
    return (addr < NVM_PARAM_SIZE) ? s_shadow[addr] : 0xFF;
}
uint16_t nvm_read_word(uint16_t addr) {
    return (uint16_t)(nvm_read_byte(addr) | ((uint16_t)nvm_read_byte((uint16_t)(addr + 1)) << 8));
}
void nvm_write_byte(uint16_t addr, uint8_t v) {
    if (addr < NVM_PARAM_SIZE && s_shadow[addr] != v) { s_shadow[addr] = v; s_dirty = true; }
}
void nvm_write_word(uint16_t addr, uint16_t v) {
    nvm_write_byte(addr, (uint8_t)(v & 0xFF));
    nvm_write_byte((uint16_t)(addr + 1), (uint8_t)(v >> 8));
}
bool nvm_dirty(void) { return s_dirty; }

int nvm_commit(void) {
    if (!s_dirty) return 0;
    uint32_t target = s_next_slot;
    /* At the first slot of a sector, erase that sector before reusing it. The latest
       record lives in the previous sector, so this never erases live data (region >= 2 sectors). */
    if ((target % s_slots_per_sector) == 0u) {
        int e = s_be->erase(s_be->ctx, target / s_slots_per_sector);
        if (e != 0) return e;
    }
    uint32_t new_seq = s_seq + 1u;
    int r = nvm_record_write(s_be, slot_addr(target), new_seq, s_shadow);
    if (r != 0) return r;
    s_seq = new_seq;
    s_next_slot = (target + 1u) % s_slots_total;
    s_dirty = false;
    return 0;
}
