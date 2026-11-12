#include "rgb.h"
#include "is31fl3293.h"
#include <stdbool.h>

static const i2c_backend_t *s_bus;
static uint16_t (*s_bus_snapshot)(uint32_t *);
static bool s_fast_pending, s_fast_used;
static uint16_t s_retry_head;
static uint16_t s_retry_log[RGB_RETRY_LOG_SLOTS][RGB_RETRY_LOG_WORDS];
/* Event actions: 1 queued, 2 verified, 3 failed, 4 line held at deadline,
   5 superseded. Flags: write failed, restore failed, readback failed. */
static uint16_t s_retry_flags;
static uint8_t s_req[3];

void rgb_set_bus_snapshot(uint16_t (*snapshot)(uint32_t *tick))
{
    s_bus_snapshot = snapshot;
}

static void log_retry(uint32_t tick, uint16_t lines, uint16_t action)
{
    ++s_retry_head;
    uint16_t *entry = s_retry_log[(uint16_t)(s_retry_head - 1u) % RGB_RETRY_LOG_SLOTS];
    entry[0] = s_retry_head;
    entry[1] = (uint16_t)tick;
    entry[2] = (uint16_t)(tick >> 16);
    entry[3] = s_req[0];
    entry[4] = s_req[1];
    entry[5] = s_req[2];
    entry[6] = (uint16_t)(action | (lines << 8));
    entry[7] = s_retry_flags;
}
static uint8_t s_app[3];
#ifdef RGB_BENCH_INJECTION
static uint16_t s_bench_arm;
static bool s_bench_held_due, s_bench_fallback;

int rgb_bench_arm(uint16_t mode)
{
    if (mode > 2u) return -1;
    if (mode && (s_app[0] != 0u || s_app[1] != 128u || s_app[2] != 93u ||
                 s_fast_pending || s_bench_fallback))
        return -1;
    s_bench_arm = mode;
    return 0;
}
#endif
static bool     s_inited, s_pending;
static bool     s_new_request;
static bool     s_verify_only;
static bool     s_restore;         /* chip state unknown after a failed transfer */
static uint8_t  s_fails;           /* consecutive failed applies/refreshes */
static uint16_t s_status;          /* PRESENT | APPLY_OK (PENDING derived) */
static uint32_t s_retry_at, s_backoff, s_refresh_at;
static uint16_t s_mode_mismatches, s_control_mismatches, s_read_errors, s_checks;
static uint16_t s_enable_head, s_enable_log[32][12];
static uint16_t s_reset_regs[5], s_configured_regs[5];
static bool s_alignment_captured;
static uint16_t s_immediate_restores, s_verified_restores;
static uint16_t s_last_mode, s_last_control, s_expected_control, s_last_mismatch;

static void increment(uint16_t *value)
{
    if (*value != UINT16_MAX) ++*value;
}

enum { VERIFY_OK, VERIFY_IO, VERIFY_MISMATCH };

static int read_verify_register(uint8_t reg, uint8_t *value, bool retry_reads)
{
    int status = s_bus->read(s_bus->ctx, reg, value, 1);
    if (status != 0 && retry_reads)
        status = s_bus->read(s_bus->ctx, reg, value, 1);
    return status;
}

static int verify_applied(uint8_t r, uint8_t g, uint8_t b, bool retry_reads)
{
    uint8_t actual[2];
    s_expected_control = is31_control_value(g, r, b);
    /* Address each register explicitly. The datasheet specifies address
       auto-increment for writes, but only illustrates a one-byte read. */
    int mode_status = read_verify_register(IS31_REG_CONFIG, &actual[1], retry_reads);
    int control_status = read_verify_register(IS31_REG_SHUTDOWN, &actual[0], retry_reads);
    if (mode_status != 0 || control_status != 0) {
        increment(&s_read_errors);
        return VERIFY_IO;
    }
    s_last_control = actual[0];
    s_last_mode = actual[1];
    increment(&s_checks);
    if (s_last_mode != IS31_CONFIG_VAL) increment(&s_mode_mismatches);
    if (s_last_control != s_expected_control) increment(&s_control_mismatches);
    if (s_last_mode != IS31_CONFIG_VAL || s_last_control != s_expected_control) {
        s_last_mismatch = (uint16_t)((s_last_mode << 8) | s_last_control);
        return VERIFY_MISMATCH;
    }
    return VERIFY_OK;
}

uint16_t rgb_get_diagnostic(uint16_t reg)
{
#ifdef RGB_BENCH_INJECTION
    if (reg == 154) return s_bench_arm;
#endif
    if (reg == 153) return s_retry_head;
    if (reg >= RGB_RETRY_LOG_BASE && reg <= RGB_RETRY_LOG_END) {
        unsigned offset = reg - RGB_RETRY_LOG_BASE;
        return s_retry_log[offset / RGB_RETRY_LOG_WORDS][offset % RGB_RETRY_LOG_WORDS];
    }
    if (reg >= 142 && reg <= 146) return s_reset_regs[reg-142];
    if (reg >= 147 && reg <= 151) return s_configured_regs[reg-147];
    if (reg >= 1504 && reg <= 1887)
        return s_enable_log[(reg-1504)/12][(reg-1504)%12];
    switch (reg) {
    case 141: return s_enable_head;
    case 152: return s_alignment_captured;

    case 84: return s_mode_mismatches;
    case 85: return s_control_mismatches;
    case 86: return s_read_errors;
    case 87: return s_last_mode;
    case 88: return s_last_control;
    case 89: return s_expected_control;
    case 90: return s_checks;
    case 91: return s_last_mismatch;
    case 139: return s_immediate_restores;
    case 140: return s_verified_restores;
    default: return 0;
    }
}

static void backoff_fail(uint32_t now)
{
    s_retry_at = now + s_backoff;
    s_backoff = (s_backoff * 2u > RGB_RETRY_MAX_MS) ? RGB_RETRY_MAX_MS : s_backoff * 2u;
}

/* A failed apply/refresh is retried after the backoff with the full
   configuration but no reset (a reset blanks a lit lamp, and re-lighting it
   from dark faults the shared bus). Only repeated failures re-initialize. */
static void transfer_failed(uint32_t now)
{
    s_status &= (uint16_t)~RGB_ST_APPLY_OK;
    s_restore = true;
    if (++s_fails >= RGB_REINIT_FAILS) s_inited = false;
    uint16_t lines = s_bus_snapshot ? s_bus_snapshot(&now) : 0;
    if (s_inited && !s_fast_used && (s_retry_flags & 1u) && lines == 3u) {
        s_fast_used = s_fast_pending = true;
        s_retry_at = now + RGB_FAST_RETRY_MS;
        log_retry(now, lines, 1u);
    } else {
        backoff_fail(now);
    }
}

uint16_t rgb_to12(uint8_t v) { return (uint16_t)(((uint32_t)v * 4095u + 127u) / 255u); }

void rgb_init(const i2c_backend_t *bus, uint32_t now)
{
    s_bus = bus;
    is31_set_pwm_hz(400u);
    s_verify_only = false;
#ifdef RGB_BENCH_INJECTION
    s_bench_arm = 0;
    s_bench_held_due = s_bench_fallback = false;
#endif
    s_bus_snapshot = 0;
    s_fast_pending = s_fast_used = false;
    s_retry_flags = s_retry_head = 0;
    for (unsigned i = 0; i < RGB_RETRY_LOG_SLOTS; ++i) s_retry_log[i][0] = 0;
    s_req[0] = s_req[1] = s_req[2] = 0;
    s_app[0] = s_app[1] = s_app[2] = 0;
    s_inited = false;
    s_pending = false;
    s_new_request = false;
    s_restore = false;
    s_fails = 0;
    s_status = 0;
    s_mode_mismatches = s_control_mismatches = s_read_errors = s_checks = 0;
    s_last_mode = s_last_control = s_expected_control = s_last_mismatch = 0;
    s_immediate_restores = s_verified_restores = 0;
    s_enable_head = 0;
    s_alignment_captured = false;
    for (unsigned i = 0; i < 5; ++i)
        s_reset_regs[i] = s_configured_regs[i] = IS31_NOT_READ;
    for (unsigned i = 0; i < 32; ++i) s_enable_log[i][0] = 0;
    s_retry_at = now;
    s_backoff = RGB_RETRY_MIN_MS;
    s_refresh_at = now + RGB_REFRESH_MS;
}

void rgb_set_logical(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_fast_pending) {
        uint32_t now;
        uint16_t lines = s_bus_snapshot(&now);
        log_retry(now, lines, 5u);
        s_fast_pending = false;
    }
#ifdef RGB_BENCH_INJECTION
    s_bench_held_due = s_bench_fallback = false;
    if (r != 0u || g == 0u) s_bench_arm = 0;
#endif
    s_verify_only = false;
    s_req[0] = r; s_req[1] = g; s_req[2] = b;
    s_pending = true;
    s_new_request = true;
}

void rgb_get_requested(uint8_t out[3]) { for (int i = 0; i < 3; i++) out[i] = s_req[i]; }
void rgb_get_applied(uint8_t out[3])   { for (int i = 0; i < 3; i++) out[i] = s_app[i]; }

uint16_t rgb_get_status(void)
{
    return (uint16_t)(s_status | (s_pending ? RGB_ST_PENDING : 0u));
}

static void log_enable(uint32_t now, const is31_enable_sample_t *sample)
{
    if (!sample->control) return;
    ++s_enable_head;
    uint16_t *entry = s_enable_log[(uint16_t)(s_enable_head-1u) % 32u];
    entry[0] = s_enable_head;
    entry[1] = (uint16_t)now;
    entry[2] = (uint16_t)(now >> 16);
    entry[3] = sample->control;
    entry[4] = sample->before_control;
    entry[5] = sample->before_mode;
    entry[6] = sample->after_mode;
    entry[7] = sample->result;
    entry[8] = sample->disabled_mode;
    entry[9] = sample->cleared_mode;
    entry[10] = sample->current_mode;
    entry[11] = sample->pwm_mode;
}

static int restore_color(uint32_t now, uint8_t r, uint8_t g, uint8_t b)
{
    is31_enable_sample_t sample = {0};
    int rc = is31_restore(s_bus, rgb_to12(g), rgb_to12(r), rgb_to12(b), &sample);
    log_enable(now, &sample);
    return rc;
}

/* One full restore in the same service pass. Backoff bounds further work. */
static int restore_and_verify(uint32_t now, uint8_t r, uint8_t g, uint8_t b)
{
    increment(&s_immediate_restores);
    int restore_status = restore_color(now, r, g, b);
    int readback_status = verify_applied(r, g, b, restore_status == 0);
    if (restore_status != 0) s_retry_flags |= 2u;
    if (readback_status != VERIFY_OK) s_retry_flags |= 4u;
    if (restore_status != 0) return -1;
    if (readback_status != VERIFY_OK) return readback_status;
    increment(&s_verified_restores);
    return 0;
}

static int verify_or_restore(uint32_t now, int write_status, uint8_t r, uint8_t g, uint8_t b)
{
    s_retry_flags = 0;
    if (write_status != 0) {
        s_retry_flags = 1u;
        return restore_and_verify(now, r, g, b);
    }
    int result = verify_applied(r, g, b, true);
    if (result != VERIFY_OK) s_retry_flags = 4u;
    return result;
}

static int retry_pwm(uint32_t now, uint8_t r, uint8_t g, uint8_t b)
{
    s_retry_flags = 0;
    if (is31_write_pwm_and_latch(s_bus, rgb_to12(g), rgb_to12(r), rgb_to12(b)) != 0) {
        s_retry_flags = 1u;
        return -1;
    }
    int result = verify_applied(r, g, b, true);
    if (result == VERIFY_OK) return 0;
    s_retry_flags = 4u;
    if (result == VERIFY_MISMATCH) return restore_and_verify(now, r, g, b);
    return result;
}

static void apply_succeeded(uint32_t now, uint8_t r, uint8_t g, uint8_t b)
{
    s_app[0] = r; s_app[1] = g; s_app[2] = b;
    s_status |= RGB_ST_PRESENT | RGB_ST_APPLY_OK;
    s_inited = true;
    s_backoff = RGB_RETRY_MIN_MS;
    s_pending = false;
    s_restore = false;
    s_verify_only = false;
    s_fast_pending = s_fast_used = false;
    s_fails = 0;
    s_refresh_at = now + RGB_REFRESH_MS;
}

void rgb_process(uint32_t now)
{
    if (s_bus == 0) return;

    /* A fresh command gets an immediate attempt despite an older backoff. */
    if (s_new_request) {
        s_new_request = false;
        s_retry_at = now;
        s_backoff = RGB_RETRY_MIN_MS;
    }

    if (!s_inited && (int32_t)(now - s_retry_at) >= 0) {
        if (is31_init(s_bus, s_alignment_captured ? 0 : s_reset_regs,
                      s_alignment_captured ? 0 : s_configured_regs) == 0) {
            s_alignment_captured = true;
            s_inited = true;
            s_status |= RGB_ST_PRESENT;
            s_pending = true;       /* re-apply the requested color after (re)init */
            s_restore = true;       /* init leaves the chip in shutdown */
            s_fails = 0;
            s_refresh_at = now + RGB_REFRESH_MS;
        } else if (verify_or_restore(now, -1, s_req[0], s_req[1], s_req[2]) == 0) {
            apply_succeeded(now, s_req[0], s_req[1], s_req[2]);
        } else {
            s_status &= (uint16_t)~(RGB_ST_PRESENT | RGB_ST_APPLY_OK);
            backoff_fail(now);
        }
    }

    /* Apply pending requests or retry after backoff. Wake with a full
       restore, and change output enables only after the new PWM is latched. */
    if (s_inited && (s_pending || s_restore) &&
        (!s_restore || (int32_t)(now - s_retry_at) >= 0)) {
        bool fast = s_fast_pending;
        if (fast) {
            s_fast_pending = false;
            uint16_t lines = s_bus_snapshot(&now);
#ifdef RGB_BENCH_INJECTION
            if (s_bench_held_due) {
                s_bench_held_due = false;
                s_bench_fallback = true;
                lines &= 1u; /* Simulate SDA low only in the lamp's gate. */
            }
#endif
            if (lines != 3u) {
                log_retry(now, lines, 4u);
                backoff_fail(now);
                return;
            }
        }
        uint8_t r = s_req[0], g = s_req[1], b = s_req[2];
#ifdef RGB_BENCH_INJECTION
        if (s_bench_arm && !s_restore && r == 0u && g == 81u && b == 58u &&
            s_app[0] == 0u && s_app[1] == 128u && s_app[2] == 93u) {
            uint16_t mode = s_bench_arm;
            s_bench_arm = 0;
            /* Inject before the initial write or immediate restore touches I2C. */
            s_retry_flags = 0x0Bu; /* Initial write and restore failed, injected. */
            transfer_failed(now);
            s_bench_held_due = s_fast_pending && mode == 2u;
            return;
        }
#endif
        if (fast) {
            uint16_t lines = s_bus_snapshot(&now);
            log_retry(now, lines, 7u);
        }
        bool was_off = !(s_app[0] || s_app[1] || s_app[2]);
        bool enables_changed = !r != !s_app[0] || !g != !s_app[1] || !b != !s_app[2];
        bool verify_only = s_verify_only;
        s_verify_only = false;
        int result;
        if (verify_only) {
            result = verify_or_restore(now, 0, r, g, b);
            if (result == VERIFY_MISMATCH)
                result = restore_and_verify(now, r, g, b);
        } else if (fast && !was_off && !enables_changed) {
            result = retry_pwm(now, r, g, b);
        } else {
            int rc;
            if (!(r || g || b))
                rc = is31_shutdown(s_bus);
            else if (s_restore || was_off)
                rc = restore_color(now, r, g, b);
            else {
                rc = is31_set_pwm(s_bus, rgb_to12(g), rgb_to12(r), rgb_to12(b));
                if (rc == 0 && enables_changed) {
                    is31_enable_sample_t sample = {0};
                    rc = is31_enable_outputs(s_bus, g, r, b, &sample);
                    log_enable(now, &sample);
                }
            }
            result = verify_or_restore(now, rc, r, g, b);
        }
        if (fast) {
            uint16_t lines = s_bus_snapshot(&now);
            log_retry(now, lines, result == 0 ? 2u : 3u);
        }
#ifdef RGB_BENCH_INJECTION
        if (s_bench_fallback) {
            s_bench_fallback = false;
            uint16_t lines = s_bus_snapshot(&now);
            log_retry(now, lines, result == 0 ? 6u : 8u);
        }
#endif
        if (result == 0)
            apply_succeeded(now, r, g, b);
        else {
            /* A successful latch followed by unreadable registers gets a
               bounded read-only pass before normal restore backoff resumes. */
            s_verify_only = !verify_only && result == VERIFY_IO && s_fails + 1u < RGB_REINIT_FAILS;
            transfer_failed(now);
        }
    }

    /* Refresh the lit configuration or reassert shutdown when idle. */
    if (s_inited && !s_pending && !s_restore && (int32_t)(now - s_refresh_at) >= 0) {
        uint8_t r = s_app[0], g = s_app[1], b = s_app[2];
        int rc = (r || g || b)
            ? restore_color(now, r, g, b)
            : is31_shutdown(s_bus);
        int result = verify_or_restore(now, rc, r, g, b);
        if (result == 0)
            apply_succeeded(now, r, g, b);
        else {
            s_verify_only = result == VERIFY_IO && s_fails + 1u < RGB_REINIT_FAILS;
            transfer_failed(now);
        }
    }
}
