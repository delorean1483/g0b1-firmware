#ifndef RGB_H
#define RGB_H
/* External RGB lamp service. One transport-independent entry (rgb_set_logical);
   all I2C happens in rgb_process() from the superloop, never blocking.
   I2C happens only when: (a) the chip is not initialized and the retry time
   has come (init, backoff 500..8000 ms); (b) a requested color is pending
   (apply: PWM + latch only); (c) every RGB_REFRESH_MS of idle time (is31_restore
   of the applied color, so a chip power glitch can't leave the lamp dark);
   (d) after a failed apply/refresh, once the backoff expires (is31_restore of
   the requested color, no reset). RGB_REINIT_FAILS consecutive failures fall
   back to (a), which resets the chip. Otherwise none.
   rgb_set_logical must be called from the main loop, not an ISR (a future BLE
   handler included); it is not interrupt-safe. */
#include "i2c_backend.h"
#include <stdint.h>

#define RGB_ST_PRESENT   0x0001u  /* chip found and initialized; stays set after an apply failure until a re-init fails */
#define RGB_ST_APPLY_OK  0x0002u  /* last apply succeeded */
#define RGB_ST_PENDING   0x0004u  /* requested color not yet applied */
#define RGB_RETRY_MIN_MS 500u
#define RGB_RETRY_MAX_MS 8000u
#define RGB_REFRESH_MS   10000u   /* idle refresh period */
#define RGB_REINIT_FAILS 3u       /* consecutive failed applies/refreshes before a reset re-init */

void     rgb_init(const i2c_backend_t *bus, uint32_t now); /* stores the bus, resets state; no I2C */
void     rgb_set_logical(uint8_t r, uint8_t g, uint8_t b); /* THE entry point for every transport */
void     rgb_get_requested(uint8_t out[3]);                /* r, g, b */
void     rgb_get_applied(uint8_t out[3]);
uint16_t rgb_get_status(void);
void     rgb_process(uint32_t now);                        /* superloop; the only place that does I2C */
uint16_t rgb_to12(uint8_t v);                              /* (v*4095+127)/255 */
#endif
