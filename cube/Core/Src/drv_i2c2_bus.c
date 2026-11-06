#include "drv_i2c2_bus.h"
#include "main.h"
#include "mb_regmodel.h"

extern I2C_HandleTypeDef hi2c2;
#define SCL_PORT GPIOA
#define SCL_PIN GPIO_PIN_11
#define SDA_PORT GPIOB
#define SDA_PIN GPIO_PIN_14
#define CLOCK_WAIT_MS 5u
#define RECOVERY_INTERVAL_MS 1000u
static uint16_t s_attempts, s_cleared, s_failed, s_last_lines;
static uint32_t s_recovery_at;
static int s_recovery_seen, s_defer;

void drv_i2c2_init(void)
{
    s_attempts = s_cleared = s_failed = s_last_lines = 0;
    s_recovery_at = 0;
    s_recovery_seen = s_defer = 0;
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
static int wait_scl_high(void)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GPIO_ReadPin(SCL_PORT, SCL_PIN) != GPIO_PIN_SET) {
        if ((uint32_t)(HAL_GetTick() - start) >= CLOCK_WAIT_MS) return -1;
        HAL_Delay(1);
    }
    return 0;
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
    if (wait_scl_high() == 0) {
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
static int transfer(int write, int retry, uint8_t addr7, uint8_t reg, uint8_t *buf,
                    uint16_t len, uint32_t timeout_ms)
{
    unsigned attempt;
    /* A failed clear or a still-held line must not pay another HAL timeout
     * on every heater/RGB poll during the shared recovery cooldown. */
    if (cooling_down() && (s_defer || line_levels() != 3u)) return -1;
    for (attempt = 0; attempt < 2u; ++attempt) {
        HAL_StatusTypeDef status = write
            ? HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(addr7 << 1), reg,
                                I2C_MEMADD_SIZE_8BIT, buf, len, timeout_ms)
            : HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(addr7 << 1), reg,
                               I2C_MEMADD_SIZE_8BIT, buf, len, timeout_ms);
        if (status == HAL_OK) { s_defer = 0; return 0; }
        /* Optional absent slaves NACK normally; don't disrupt an idle bus. */
        if (status == HAL_ERROR && HAL_I2C_GetError(&hi2c2) == HAL_I2C_ERROR_AF &&
            line_levels() == 3u && !(hi2c2.Instance->ISR & I2C_ISR_BUSY)) return -1;
        if (attempt != 0u) { s_defer = 1; return -1; }
        if (cooling_down()) { s_defer = 1; return -1; }
        s_defer = (recover_bus() != 0);
        s_recovery_seen = 1;
        s_recovery_at = HAL_GetTick(); /* At least 1 s after this clear ends. */
        /* An errored write may already have reached the slave. Free the bus
         * for other clients, but never replay a side-effecting command. */
        if (s_defer || !retry) return -1;
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
}
