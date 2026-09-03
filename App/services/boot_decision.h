#ifndef BOOT_DECISION_H
#define BOOT_DECISION_H
#include <stdint.h>
#include "bootcfg.h"

#define BOOT_MAGIC_ENTER  0xB00710ADu
#define TRIAL_BOOT_LIMIT  3u

typedef enum { BOOT_ACTION_ENTER_UPDATE = 0, BOOT_ACTION_JUMP_SLOT = 1 } boot_action_t;
typedef struct { boot_action_t action; slot_id_t slot; int cfg_dirty; } boot_decision_t;

boot_decision_t boot_decide(bootcfg_t *cfg, int cfg_valid, uint32_t reset_magic,
                            int (*slot_ok)(void *v, slot_id_t s), void *v);
#endif
