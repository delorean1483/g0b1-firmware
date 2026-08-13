#ifndef MBP_SENSORS_H
#define MBP_SENSORS_H
#include "rpm.h"
/* Register the sensor read-only providers (Modbus regs 1,3,6,38,51).
   rpm_src supplies reg 38; pass the configured RPM source (may be NULL -> reg 38 reads 0). */
void mbp_sensors_register(const rpm_source_t *rpm_src);
#endif /* MBP_SENSORS_H */
