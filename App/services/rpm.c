#include "rpm.h"
#include "sensors_cal.h"

uint16_t rpm_read(const rpm_source_t *src) {
    if (src == 0 || src->get_rpm == 0) return 0;
    return src->get_rpm(src->ctx);
}

rpm_engine_state_t rpm_classify(uint16_t rpm) {
    if (rpm == 0) return RPM_ENGINE_NONE;
    if (rpm < ENGINE_RPM_LOW_LIMIT) return RPM_ENGINE_LOW;
    return RPM_ENGINE_RUNNING;
}
