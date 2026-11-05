#include "rgb.h"
#include "is31fl3293.h"
#include <stdbool.h>

static const i2c_backend_t *s_bus;
static uint8_t  s_req[3], s_app[3];
static bool     s_inited, s_pending;
static uint16_t s_status;          /* PRESENT | APPLY_OK (PENDING derived) */
static uint32_t s_retry_at, s_backoff;

uint16_t rgb_to12(uint8_t v) { return (uint16_t)(((uint32_t)v * 4095u + 127u) / 255u); }

void rgb_init(const i2c_backend_t *bus, uint32_t now)
{
    s_bus = bus;
    s_req[0] = s_req[1] = s_req[2] = 0;
    s_app[0] = s_app[1] = s_app[2] = 0;
    s_inited = false;
    s_pending = false;
    s_status = 0;
    s_retry_at = now;
    s_backoff = RGB_RETRY_MIN_MS;
}

void rgb_set_logical(uint8_t r, uint8_t g, uint8_t b)
{
    s_req[0] = r; s_req[1] = g; s_req[2] = b;
    s_pending = true;
}

void rgb_get_requested(uint8_t out[3]) { for (int i = 0; i < 3; i++) out[i] = s_req[i]; }
void rgb_get_applied(uint8_t out[3])   { for (int i = 0; i < 3; i++) out[i] = s_app[i]; }

uint16_t rgb_get_status(void)
{
    return (uint16_t)(s_status | (s_pending ? RGB_ST_PENDING : 0u));
}

void rgb_process(uint32_t now)
{
    if (s_bus == 0) return;

    if (!s_inited && (int32_t)(now - s_retry_at) >= 0) {
        if (is31_init(s_bus) == 0) {
            s_inited = true;
            s_status |= RGB_ST_PRESENT;
            s_backoff = RGB_RETRY_MIN_MS;
            s_pending = true;       /* re-apply the requested color after (re)init */
        } else {
            s_status &= (uint16_t)~RGB_ST_PRESENT;
            s_retry_at = now + s_backoff;
            s_backoff = (s_backoff * 2u > RGB_RETRY_MAX_MS) ? RGB_RETRY_MAX_MS : s_backoff * 2u;
        }
    }

    if (s_inited && s_pending) {
        uint8_t r = s_req[0], g = s_req[1], b = s_req[2];
        if (is31_set_pwm(s_bus, rgb_to12(g), rgb_to12(r), rgb_to12(b)) == 0) {
            s_app[0] = r; s_app[1] = g; s_app[2] = b;
            s_status |= RGB_ST_APPLY_OK;
            s_pending = false;
        } else {
            s_status &= (uint16_t)~RGB_ST_APPLY_OK;
            s_inited = false;
            s_retry_at = now;
        }
    }
}
