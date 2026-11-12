#include "drv_i2c2_bus.h"
#include "main.h"
#include "mb_regmodel.h"
#include "i2c_bench_trace.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c2;
#define SCL_PORT GPIOA
#define SCL_PIN GPIO_PIN_11
#define SDA_PORT GPIOB
#define SDA_PIN GPIO_PIN_14
#define CLOCK_WAIT_MS 5u
#define RECOVERY_INTERVAL_MS 1000u
#define RECOVERY_FAILURE_THRESHOLD 3u
static uint16_t s_attempts, s_cleared, s_failed, s_last_lines;
static uint32_t s_recovery_at;
static unsigned s_transfer_failures;
static int s_recovery_seen, s_clear_failed;
static drv_i2c2_result_t s_result;
/* Per device: HAL attempts, success, ARLO, AF, timeout, busy, other error. */
static uint32_t s_is31_write_attempts, s_is31_write_failures;
static uint16_t s_stats[2][7], s_deferred[2], s_event_sequence;
static uint16_t s_guard_hits[2], s_guard_recoveries;
static uint16_t s_events[32][10];
static uint16_t s_phase_events[32][32];
static struct {
    int active, first;
    uint16_t stage, words[32], length;
    uintptr_t buffer;
} s_trace;

const drv_i2c2_result_t *drv_i2c2_last_result(void) { return &s_result; }

void drv_i2c2_init(void)
{
    s_attempts = s_cleared = s_failed = s_last_lines = 0;
    s_recovery_at = 0;
    s_transfer_failures = 0;
    s_recovery_seen = s_clear_failed = 0;
    memset(&s_result, 0, sizeof(s_result));
    s_is31_write_attempts = s_is31_write_failures = 0;
    memset(s_stats, 0, sizeof(s_stats));
    memset(s_deferred, 0, sizeof(s_deferred));
    memset(s_events, 0, sizeof(s_events));
    memset(s_phase_events, 0, sizeof(s_phase_events));
    memset(&s_trace, 0, sizeof(s_trace));
    s_event_sequence = 0;
    memset(s_guard_hits, 0, sizeof(s_guard_hits));
    s_guard_recoveries = 0;
}
static int cooling_down(void)
{
    return s_recovery_seen &&
        (uint32_t)(HAL_GetTick() - s_recovery_at) < RECOVERY_INTERVAL_MS;
}

static void increment(uint16_t *v) { if (*v != UINT16_MAX) ++*v; }
static uint16_t line_levels(void)
{
    return (uint16_t)((HAL_GPIO_ReadPin(SCL_PORT, SCL_PIN) == GPIO_PIN_SET ? 1u : 0u)
                    | (HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_SET ? 2u : 0u));
}

uint16_t drv_i2c2_snapshot(uint32_t *tick)
{
    *tick = HAL_GetTick();
    return line_levels();
}

static int device_index(uint8_t address) { return address == 0x6c ? 0 : 1; }
static void put32(uint16_t *p, uint32_t value)
{ p[0]=(uint16_t)value; p[1]=(uint16_t)(value >> 16); }

void i2c_bench_stage(void *handle, uint16_t stage)
{
    if (handle == &hi2c2 && s_trace.active) s_trace.stage = stage;
}
void i2c_bench_error(void *handle, uint32_t isr, uint16_t reason)
{
    if (handle != &hi2c2 || !s_trace.active) return;
    uint16_t *w=s_trace.words;
    uint32_t observed=(uint32_t)w[28] | ((uint32_t)w[29] << 16);
    put32(w+28, observed | isr);
    if ((w[30] & 0x7fffu) < 0x7fffu) ++w[30];
    if (reason == 1) w[30] |= 0x8000u;
    if (s_trace.first) return;
    s_trace.first=1;
    w[9]=(uint16_t)(s_trace.stage | (reason == 2 ? 0x8000u : 0u));
    put32(w+10,isr); put32(w+12,hi2c2.Instance->CR2);
    w[14]=line_levels(); put32(w+15,HAL_GetTick());
    /* Initial BUSY/entry failures precede HAL's current-call buffer setup. */
    if (s_trace.stage <= I2C_TRACE_BUSY) {
        w[17]=w[18]=w[19]=UINT16_MAX;
    } else {
        w[17]=hi2c2.XferCount; w[18]=hi2c2.XferSize;
        uintptr_t ptr=(uintptr_t)hi2c2.pBuffPtr;
        w[19]=(ptr >= s_trace.buffer && ptr-s_trace.buffer <= s_trace.length)
            ? (uint16_t)(ptr-s_trace.buffer) : UINT16_MAX;
    }
    w[21]=reason; put32(w+22,hi2c2.Instance->CR1);
    w[25]=(uint16_t)hi2c2.State;
}
static void trace_begin(uint8_t *buffer, uint16_t length)
{
    memset(&s_trace,0,sizeof(s_trace));
    s_trace.active=1; s_trace.stage=I2C_TRACE_ENTRY;
    s_trace.buffer=(uintptr_t)buffer; s_trace.length=length;
    uint16_t *w=s_trace.words;
    w[1]=1; w[20]=length; put32(w+2,hi2c2.Instance->ISR);
    put32(w+4,hi2c2.Instance->CR2); w[6]=line_levels();
    put32(w+7,HAL_GetTick()); w[24]=(uint16_t)hi2c2.State;
    put32(w+26,hi2c2.ErrorCode); w[21]=3;
}

static void log_result(int write, unsigned retry, uint8_t address, uint8_t reg,
                       const uint8_t *data, uint16_t len, HAL_StatusTypeDef status)
{
    uint16_t *stats = s_stats[device_index(address)];
    if (write && address == 0x6Cu) {
        ++s_is31_write_attempts;
        if (status != HAL_OK) ++s_is31_write_failures;
    }
    s_result.attempted = 1;
    s_result.status = (uint16_t)status;
    s_result.error = status == HAL_OK || status == HAL_BUSY ? 0 : HAL_I2C_GetError(&hi2c2);
    s_result.isr = hi2c2.Instance->ISR; /* After HAL returns; flags may be cleared. */
    s_result.tick = HAL_GetTick();
    s_result.lines = line_levels();
    if (status != HAL_OK && !s_trace.first)
        i2c_bench_error(&hi2c2, s_result.isr, 2); /* Explicit post-return fallback. */
    s_trace.words[31]=s_trace.stage;
    s_trace.active=0;
    increment(&stats[0]);
    if (status == HAL_OK) { increment(&stats[1]); return; }
    if (s_result.error & HAL_I2C_ERROR_ARLO) increment(&stats[2]);
    if (s_result.error & HAL_I2C_ERROR_AF) increment(&stats[3]);
    if (status == HAL_TIMEOUT || (s_result.error & HAL_I2C_ERROR_TIMEOUT)) increment(&stats[4]);
    if (status == HAL_BUSY) increment(&stats[5]);
    if (status == HAL_ERROR &&
        !(s_result.error & (HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_AF | HAL_I2C_ERROR_TIMEOUT)))
        increment(&stats[6]);
    /* Natural 16-bit sequence wrap; each slot retains its own sequence. */
    ++s_event_sequence;
    uint16_t *event = s_events[(uint16_t)(s_event_sequence - 1u) % 32u];
    event[0] = s_event_sequence;
    event[1] = (uint16_t)s_result.tick;
    event[2] = (uint16_t)(s_result.tick >> 16);
    event[3] = (uint16_t)((address << 8) | reg);
    event[4] = (uint16_t)s_result.error;
    event[5] = (uint16_t)(status | (write ? 0x10u : 0u) |
                         (retry ? 0x20u : 0u) | (s_result.lines << 8));
    event[6] = (uint16_t)s_result.isr;
    event[7] = (uint16_t)(s_result.isr >> 16);
    event[8] = len;
    event[9] = write && len ? (uint16_t)((data[0] << 8) | data[len - 1]) : 0;
    s_trace.words[0]=s_event_sequence;
    memcpy(s_phase_events[(uint16_t)(s_event_sequence-1u)%32u],
           s_trace.words,sizeof(s_trace.words));
}
static int wait_scl_high(void)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(SCL_PORT, SCL_PIN) != GPIO_PIN_SET) {
        if ((uint32_t)(HAL_GetTick() - start) >= CLOCK_WAIT_MS) return -1;
        HAL_Delay(1);
    }
    return 0;
}
static int refresh_idle_lines(void)
{
    /* ES0348 F1 filter workaround, adapted to the G0 peripheral reset.
     * Only generate this START/STOP sequence once both wires are released. */
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    if (line_levels() != 1u) return 0;
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    if (line_levels() != 0u) return 0;
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    if (wait_scl_high() != 0) return 0;
    HAL_Delay(1);
    if (line_levels() != 1u) return 0;
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    return line_levels() == 3u;
}
static int recover_bus(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t analog = (hi2c2.Instance->CR1 & I2C_CR1_ANFOFF)
                    ? I2C_ANALOGFILTER_DISABLE : I2C_ANALOGFILTER_ENABLE;
    uint32_t digital = (hi2c2.Instance->CR1 & I2C_CR1_DNF) >> I2C_CR1_DNF_Pos;
    int clear = 0;
    increment(&s_attempts);
    s_last_lines = line_levels();
    /* PE off releases a clock held by the master; DeInit clears HAL state.
     * Neither slave nor the MCU is reset. */
    __HAL_I2C_DISABLE(&hi2c2);
    (void)HAL_I2C_DeInit(&hi2c2);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* Preload high before switching modes: all highs are open-drain releases. */
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = SCL_PIN;
    HAL_GPIO_Init(SCL_PORT, &gpio);
    gpio.Pin = SDA_PIN;
    HAL_GPIO_Init(SDA_PORT, &gpio);
    HAL_Delay(1);
    if (line_levels() == 3u) {
        clear = refresh_idle_lines();
    } else if (wait_scl_high() == 0) {
        unsigned pulses;
        for (pulses = 0; pulses < 9u &&
             HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN) == GPIO_PIN_RESET; ++pulses) {
            HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET);
            HAL_Delay(1);
            HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
            if (wait_scl_high() != 0) break;
            HAL_Delay(1);
        }
        /* STOP: SDA rises with SCL high. SDA is set low with SCL low first,
         * avoiding an accidental START. External stretching is bounded. */
        if (wait_scl_high() == 0) {
            HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_RESET);
            HAL_Delay(1);
            HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
            if (wait_scl_high() == 0) {
                HAL_Delay(1);
                HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
                HAL_Delay(1);
                clear = (line_levels() == 3u);
            }
        }
    }
    /* Always release outputs and restore AF/HAL even for a physical short.
     * Reset only I2C2's peripheral registers to clear stale BUSY/errors. */
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();
    /* DeInit left state RESET: MspInit restores PA11/PB14 AF6. Init restores
     * saved timing; restore the original analog/digital filter configuration. */
    if (HAL_I2C_Init(&hi2c2) != HAL_OK ||
        HAL_I2CEx_ConfigAnalogFilter(&hi2c2, analog) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&hi2c2, digital) != HAL_OK) clear = 0;
    if (clear) increment(&s_cleared); else increment(&s_failed);
    return clear ? 0 : -1;
}
static int recover_with_cooldown(void)
{
    s_clear_failed = (recover_bus() != 0);
    if (!s_clear_failed) s_transfer_failures = 0;
    s_recovery_seen = 1;
    s_recovery_at = HAL_GetTick();
    return !s_clear_failed;
}

static int prepare_transfer(uint8_t addr7, uint8_t *buffer, uint16_t length)
{
    int device = device_index(addr7);
    trace_begin(buffer, length);
    if (s_trace.words[6] != 3u) {
        increment(&s_guard_hits[device]);
        s_trace.active = 0;
        if (cooling_down()) goto defer;

        increment(&s_guard_recoveries);
        if (!recover_with_cooldown()) goto defer;
        trace_begin(buffer, length);
    }

    /* Check the pins again after a clear, once I2C owns them. */
    if (s_trace.words[6] == 3u && !(s_clear_failed && cooling_down())) return 1;

defer:
    s_trace.active = 0;
    if (!s_result.attempted) increment(&s_deferred[device]);
    return 0;
}

static int transfer(int write, int retry, uint8_t addr7, uint8_t reg, uint8_t *buf,
                    uint16_t len, uint32_t timeout_ms)
{
    unsigned attempt;
    s_result.attempted = 0;
    s_result.status = HAL_BUSY;
    s_result.error = 0;
    for (attempt = 0; attempt < 2u; ++attempt) {
        if (!prepare_transfer(addr7, buf, len)) return -1;
        /* Blocking HAL checks BUSY before resetting ErrorCode and ORs timeout
         * into it on that path. Clear the previous operation's flags here so
         * a stuck-bus follower cannot inherit another slave's ARLO/NACK. */
        hi2c2.ErrorCode = 0;
        HAL_StatusTypeDef status = write
            ? HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(addr7 << 1), reg,
                                I2C_MEMADD_SIZE_8BIT, buf, len, timeout_ms)
            : HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(addr7 << 1), reg,
                               I2C_MEMADD_SIZE_8BIT, buf, len, timeout_ms);
        log_result(write, attempt, addr7, reg, buf, len, status);
        if (status == HAL_OK) {
            s_clear_failed = 0;
            s_transfer_failures = 0;
            return 0;
        }
        /* Optional absent slaves NACK normally; don't disrupt an idle bus. */
        if (status == HAL_ERROR && HAL_I2C_GetError(&hi2c2) == HAL_I2C_ERROR_AF &&
            line_levels() == 3u && !(hi2c2.Instance->ISR & I2C_ISR_BUSY)) {
            s_transfer_failures = 0;
            return -1;
        }
        if (s_transfer_failures < RECOVERY_FAILURE_THRESHOLD) ++s_transfer_failures;
        if (s_transfer_failures < RECOVERY_FAILURE_THRESHOLD ||
            attempt != 0u || cooling_down()) return -1;
        /* Recover for both clients, but never replay a side-effecting write. */
        if (!recover_with_cooldown() || !retry) return -1;
    }
    return -1;
}
int drv_i2c2_read(uint8_t a, uint8_t r, uint8_t *b, uint16_t n, uint32_t t)
{ return transfer(0, 1, a, r, b, n, t); }
int drv_i2c2_write(uint8_t a, uint8_t r, const uint8_t *b, uint16_t n, uint32_t t)
{ return transfer(1, 1, a, r, (uint8_t *)b, n, t); }
int drv_i2c2_write_once(uint8_t a, uint8_t r, const uint8_t *b, uint16_t n, uint32_t t)
{ return transfer(1, 0, a, r, (uint8_t *)b, n, t); }
static modbus_exc_t read_diagnostic(uint16_t reg, uint16_t *out)
{
    if (reg >= 92 && reg <= 105) {
        *out = s_stats[(reg - 92u) / 7u][(reg - 92u) % 7u];
        return MB_EXC_NONE;
    }
    if (reg == 106 || reg == 107) {
        *out = s_deferred[reg - 106u]; return MB_EXC_NONE;
    }
    if (reg == 136 || reg == 137) { *out = s_guard_hits[reg-136u]; return MB_EXC_NONE; }
    if (reg == 138) { *out = s_guard_recoveries; return MB_EXC_NONE; }
    if (reg >= 156u && reg <= 159u) {
        uint32_t count = reg <= 157u ? s_is31_write_attempts : s_is31_write_failures;
        *out = (uint16_t)(count >> ((reg & 1u) ? 16u : 0u));
        return MB_EXC_NONE;
    }
    if (reg == 135) { *out = s_event_sequence; return MB_EXC_NONE; }
    if (reg >= 160 && reg <= 479) {
        *out = s_events[(reg - 160u) / 10u][(reg - 160u) % 10u];
        return MB_EXC_NONE;
    }
    if (reg >= 480 && reg <= 1503) {
        *out=s_phase_events[(reg-480u)/32u][(reg-480u)%32u];
        return MB_EXC_NONE;
    }
    switch (reg) {
    case 80: *out = s_attempts; break;
    case 81: *out = s_cleared; break;
    case 82: *out = s_failed; break;
    case 83: *out = s_last_lines; break;
    default: return MB_EXC_ILLEGAL_ADDRESS;
    }
    return MB_EXC_NONE;
}
void drv_i2c2_register_diagnostics(void)
{
    uint16_t reg;
    for (reg = 80; reg <= 83; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
    for (reg = 92; reg <= 107; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
    for (reg = 135; reg <= 138; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
    for (reg = 156; reg <= 159; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
    for (reg = 160; reg <= 479; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
    for (reg = 480; reg <= 1503; ++reg) mb_reg_bind(reg, read_diagnostic, 0);
}
