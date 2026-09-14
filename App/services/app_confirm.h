#ifndef APP_CONFIRM_H
#define APP_CONFIRM_H
#include "iflash_backend.h"

/* Consecutive healthy 1s ticks required before the active TRIAL slot is
   self-confirmed COMMITTED (counterpart to the bootloader's trial/revert). */
#define APP_CONFIRM_HEALTHY_SECS 5

/* Loads bootcfg; if the active slot's state == SLOT_STATE_TRIAL, arms
   confirm (and resets the healthy-second counter); else disarms. */
void app_confirm_init(const iflash_backend_t *fl);

/* Call once per 1s control slot with a health predicate. No-op when
   disarmed. healthy==0 resets the healthy-second counter. After
   APP_CONFIRM_HEALTHY_SECS consecutive healthy ticks, marks the active
   slot COMMITTED, trial_count=0, bootcfg_save; disarms on save success
   (stays armed to retry next tick on save failure). */
void app_confirm_tick(int healthy);

/* Test/inspection: 1 if armed (waiting to confirm), else 0. */
int  app_confirm_is_armed(void);

#endif
