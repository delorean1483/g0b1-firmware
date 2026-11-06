#ifndef RGB_H
#define RGB_H
/* External RGB lamp service. One transport-independent entry (rgb_set_logical);
   all I2C happens in rgb_process() from the superloop, never blocking.
   I2C happens only when: (a) the chip is not initialized and the retry time
   has come (restore, backoff 500..8000 ms); (b) a requested color is pending
   (PWM-only apply); (c) every RGB_REFRESH_MS of idle time. Failure retries and
   idle refresh write full configuration plus PWM (is31_restore, no reset).
   A chip that loses configuration but still ACKs may wait for idle refresh;
   normal color writes intentionally do not restore it. Only the first startup attempt
   calls the destructive initializer; all failure retries use restore, even
   after a failed startup attempt. Otherwise none.
   rgb_set_logical must be called from the main loop, not an ISR (a future BLE
   handler included); it is not interrupt-safe. */
#include "i2c_backend.h"
#include <stdint.h>

#define RGB_ST_PRESENT   0x0001u  /* last config succeeded; retained after initial apply failure, cleared if retry fails */
#define RGB_ST_APPLY_OK  0x0002u  /* last apply succeeded */
#define RGB_ST_PENDING   0x0004u  /* requested color not yet applied */
#define RGB_RETRY_MIN_MS 500u
#define RGB_RETRY_MAX_MS 8000u
#define RGB_REFRESH_MS   10000u   /* idle full-config refresh period */

void     rgb_init(const i2c_backend_t *bus, uint32_t now); /* stores the bus, resets state; no I2C */
void     rgb_set_logical(uint8_t r, uint8_t g, uint8_t b); /* THE entry point for every transport */
void     rgb_get_requested(uint8_t out[3]);                /* r, g, b */
void     rgb_get_applied(uint8_t out[3]);
uint16_t rgb_get_status(void);
void     rgb_process(uint32_t now);                        /* superloop; the only place that does I2C */
uint16_t rgb_to12(uint8_t v);                              /* (v*4095+127)/255 */
#endif
