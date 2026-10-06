/* Control lines, line sense and UART pin attach. Levels come from htr_board.h. */
#include "port.h"
#include "htr_board.h"
#include "htr_types.h"

#define MODE_INPUT  0u
#define MODE_OUTPUT 1u
#define MODE_AF     2u
#define MODE_ANALOG 3u

static void pin_mode(GPIO_TypeDef *p, uint32_t pin, uint32_t mode)
{
    p->MODER = (p->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}
static void pin_af(GPIO_TypeDef *p, uint32_t pin, uint32_t af)
{
    volatile uint32_t *r = &p->AFR[pin >> 3];
    uint32_t sh = (pin & 7u) * 4u;
    *r = (*r & ~(0xFu << sh)) | (af << sh);
}
static void pin_write(GPIO_TypeDef *p, uint32_t pin, bool high)
{
    p->BSRR = high ? (1u << pin) : (1u << (pin + 16u));
}
static void line_level(GPIO_TypeDef *p, uint32_t pin, bool active_hi, bool active)
{
    pin_write(p, pin, active ? active_hi : !active_hi);
}

static void g_set_line(void *ctx, htr_line_t l, bool active)
{
    (void)ctx;
    switch (l) {
    case HTR_LINE_TX_MODE: line_level(HB_TX_MODE_PORT, HB_TX_MODE_PIN, HB_TX_MODE_ACTIVE_HI, active); break;
    case HTR_LINE_TX_EN:   line_level(HB_TX_EN_PORT, HB_TX_EN_PIN, HB_TX_EN_ACTIVE_HI, active); break;
    default:               line_level(HB_TX_PWR_PORT, HB_TX_PWR_PIN, HB_TX_PWR_ACTIVE_HI, active); break;
    }
}

static void g_attach(void *ctx, uint8_t wiring, bool attach)
{
    (void)ctx;
    if (wiring == HTR_WIRING_3W) {
        pin_mode(HB_3W_TX_PORT, HB_3W_TX_PIN, attach ? MODE_AF : MODE_INPUT);
        pin_mode(HB_3W_RX_PORT, HB_3W_RX_PIN, attach ? MODE_AF : MODE_INPUT);
    } else if (wiring == HTR_WIRING_4W) {
        pin_mode(HB_4W_TX_PORT, HB_4W_TX_PIN, attach ? MODE_AF : MODE_INPUT);
        pin_mode(HB_4W_RX_PORT, HB_4W_RX_PIN, attach ? MODE_AF : MODE_INPUT);
    }
}

static bool g_rx_level(void *ctx, uint8_t wiring)
{
    (void)ctx;
    if (wiring == HTR_WIRING_4W) return (HB_4W_RX_PORT->IDR & (1u << HB_4W_RX_PIN)) != 0u;
    return (HB_3W_RX_PORT->IDR & (1u << HB_3W_RX_PIN)) != 0u;
}

const htr_gpio_t port_gpio = { g_set_line, g_attach, g_rx_level, 0 };

void port_gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    /* Output latches to the INACTIVE level first, then switch to output, so
       no control line ever glitches active during boot. */
    g_set_line(0, HTR_LINE_TX_EN, false);
    g_set_line(0, HTR_LINE_TX_PWR, false);
    g_set_line(0, HTR_LINE_TX_MODE, false);
    pin_mode(HB_TX_EN_PORT, HB_TX_EN_PIN, MODE_OUTPUT);
    pin_mode(HB_TX_PWR_PORT, HB_TX_PWR_PIN, MODE_OUTPUT);
    pin_mode(HB_TX_MODE_PORT, HB_TX_MODE_PIN, MODE_OUTPUT);
    /* UART pins: AF numbers preset, pins stay inputs until attach. */
    pin_af(HB_3W_TX_PORT, HB_3W_TX_PIN, HB_3W_AF);
    pin_af(HB_3W_RX_PORT, HB_3W_RX_PIN, HB_3W_AF);
    pin_af(HB_4W_TX_PORT, HB_4W_TX_PIN, HB_4W_AF);
    pin_af(HB_4W_RX_PORT, HB_4W_RX_PIN, HB_4W_AF);
    g_attach(0, HTR_WIRING_3W, false);
    g_attach(0, HTR_WIRING_4W, false);
#if HB_GPIOX_DEBUG
    pin_write(HB_GPIOX6_PORT, HB_GPIOX6_PIN, false);
    pin_write(HB_GPIOX7_PORT, HB_GPIOX7_PIN, false);
    pin_mode(HB_GPIOX6_PORT, HB_GPIOX6_PIN, MODE_OUTPUT);
    pin_mode(HB_GPIOX7_PORT, HB_GPIOX7_PIN, MODE_OUTPUT);
#endif
    /* I2C1 pins: AF, open-drain (external 2.2k pull-ups). */
    GPIOA->OTYPER |= (1u << HB_I2C_SCL_PIN) | (1u << HB_I2C_SDA_PIN);
    pin_af(GPIOA, HB_I2C_SCL_PIN, HB_I2C_AF);
    pin_af(GPIOA, HB_I2C_SDA_PIN, HB_I2C_AF);
    pin_mode(GPIOA, HB_I2C_SCL_PIN, MODE_AF);
    pin_mode(GPIOA, HB_I2C_SDA_PIN, MODE_AF);
}
