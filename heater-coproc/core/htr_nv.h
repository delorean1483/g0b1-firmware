#ifndef HTR_NV_H
#define HTR_NV_H
/* Persisted settings seam: L011 data EEPROM, C011 one flash page, tests a fake. */
#include <stdbool.h>
#include <stdint.h>
typedef struct htr_nv {
    /* host_timeout_min is the map v1 minutes byte: ignored on load since v2,
       saved as HTR_NV_LEGACY_TIMEOUT_MIN so a rolled-back v1 image keeps a timeout. */
    bool (*load)(void *ctx, uint8_t *type_setting, uint8_t *host_timeout_min); /* false = blank/corrupt */
    void (*save)(void *ctx, uint8_t type_setting, uint8_t host_timeout_min);
    void *ctx;
} htr_nv_t;
#define HTR_NV_DEFAULT_TYPE     0xFFu   /* HTR_TYPE_AUTO */
#define HTR_NV_LEGACY_TIMEOUT_MIN 10u
#endif
