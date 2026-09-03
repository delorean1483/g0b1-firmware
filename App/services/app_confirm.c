#include "app_confirm.h"
#include "bootcfg.h"

static const iflash_backend_t *s_fl;
static bootcfg_t s_cfg;
static int s_armed;
static int s_healthy_secs;

void app_confirm_init(const iflash_backend_t *fl) {
    s_fl = fl;
    s_armed = 0;
    s_healthy_secs = 0;
    if (bootcfg_load(fl, &s_cfg) != 0) return;
    slot_id_t a = (slot_id_t)s_cfg.active_slot;
    s_armed = (s_cfg.slot[a].state == SLOT_STATE_TRIAL) ? 1 : 0;
}

int app_confirm_is_armed(void) { return s_armed; }

void app_confirm_tick(int healthy) {
    if (!s_armed) return;
    if (!healthy) { s_healthy_secs = 0; return; }
    if (++s_healthy_secs < APP_CONFIRM_HEALTHY_SECS) return;
    slot_id_t a = (slot_id_t)s_cfg.active_slot;
    s_cfg.slot[a].state = SLOT_STATE_COMMITTED;
    s_cfg.trial_count = 0;
    if (bootcfg_save(s_fl, &s_cfg) == 0) s_armed = 0;  /* retry next tick if save fails */
}
