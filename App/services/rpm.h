#ifndef RPM_H
#define RPM_H
#include "types.h"

/* Engine status classified from RPM (debounce/anti-stall live in the control
 * milestone). Threshold ENGINE_RPM_LOW_LIMIT is in sensors_cal.h. */
typedef enum { RPM_ENGINE_NONE = 0, RPM_ENGINE_LOW, RPM_ENGINE_RUNNING } rpm_engine_state_t;

/* Pluggable RPM source: timer input-capture (default) or ADC IN6 on target;
 * a fake in host tests. The capture implementation is deferred to bring-up. */
typedef struct rpm_source { uint16_t (*get_rpm)(void *ctx); void *ctx; } rpm_source_t;

uint16_t           rpm_read(const rpm_source_t *src);
rpm_engine_state_t rpm_classify(uint16_t rpm);

#endif /* RPM_H */
