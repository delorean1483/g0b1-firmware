#include "boot_decision.h"

static boot_decision_t jump(slot_id_t s, int dirty) {
    boot_decision_t d = { BOOT_ACTION_JUMP_SLOT, s, dirty }; return d;
}
static boot_decision_t update(int dirty) {
    boot_decision_t d = { BOOT_ACTION_ENTER_UPDATE, SLOT_A, dirty }; return d;
}

boot_decision_t boot_decide(bootcfg_t *cfg, int cfg_valid, uint32_t reset_magic,
                            int (*slot_ok)(void *v, slot_id_t s), void *v) {
    if (reset_magic == BOOT_MAGIC_ENTER) return update(0);
    if (!cfg_valid) return update(0);

    int dirty = 0;
    slot_id_t active = (slot_id_t)cfg->active_slot;
    slot_id_t other  = (active == SLOT_A) ? SLOT_B : SLOT_A;

    if (cfg->slot[active].state == SLOT_STATE_TRIAL) {
        cfg->trial_count++; dirty = 1;
        if (cfg->trial_count > TRIAL_BOOT_LIMIT) {
            cfg->slot[active].state = SLOT_STATE_BAD;
            if (cfg->slot[other].state == SLOT_STATE_COMMITTED && slot_ok(v, other)) {
                cfg->active_slot = (uint16_t)other; active = other;
                other = (active == SLOT_A) ? SLOT_B : SLOT_A;
            } else {
                return update(1);
            }
        }
    }

    if (cfg->slot[active].state != SLOT_STATE_BAD && slot_ok(v, active))
        return jump(active, dirty);

    if (cfg->slot[other].state != SLOT_STATE_BAD && slot_ok(v, other)) {
        cfg->active_slot = (uint16_t)other; dirty = 1;
        return jump(other, dirty);
    }
    return update(dirty);
}
