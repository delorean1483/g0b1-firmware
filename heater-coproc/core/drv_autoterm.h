#ifndef DRV_AUTOTERM_H
#define DRV_AUTOTERM_H
#include "htr_driver.h"
extern const htr_driver_t drv_autoterm;

/* Link timing (ms), exposed for tests. */
#define AT_WAKE_COUNT        35u
#define AT_WAKE_GAP_MS      200u
#define AT_VER4800_COUNT      5u
#define AT_VER_GAP_MS       200u
#define AT_VER9600_TIMEOUT_MS 3000u
#define AT_WAKE_ATTEMPTS      3u
#define AT_TX_SLOT_MS       250u
#define AT_STATUS_EVERY_MS 1000u
#define AT_SETTINGS_EVERY_MS 5000u
#define AT_TEMP_EVERY_MS  10000u
#define AT_CMD_RETRY_MS    1000u
#define AT_START_TRIES       10u
#define AT_LINK_LOST_MS   10000u
#endif
