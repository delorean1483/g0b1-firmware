#include "wk2132.h"

#define WK2132_ADDR_FIXED       0x10U
#define WK2132_OBJECT_REGISTER  0x00U
#define WK2132_OBJECT_FIFO      0x01U

#define WK2132_REG_GENA         0x00U
#define WK2132_REG_GRST         0x01U
#define WK2132_REG_SPAGE        0x03U
#define WK2132_REG_SCR          0x04U
#define WK2132_REG_LCR          0x05U
#define WK2132_REG_FCR          0x06U
#define WK2132_REG_SIER         0x07U
#define WK2132_REG_RFCNT        0x0AU
#define WK2132_REG_FSR          0x0BU

/* Same addresses on SPAGE=1. */
#define WK2132_REG_BAUD1        0x04U
#define WK2132_REG_BAUD0        0x05U
#define WK2132_REG_PRES         0x06U

#define WK2132_FSR_TX_FULL      (1U << 1)
#define WK2132_FSR_RX_DATA      (1U << 3)

#define WK2132_XTAL_HZ          14745600UL
#define WK2132_UART_BAUD         4800UL
#define WK2132_MAX_FIFO         256U

/* Returns the 7-bit device address (channel/object encoded). The on-target
   backend shifts this for HAL_I2C_*(); the fake decodes it directly. */
static uint8_t hal_addr(const WK2132_t *dev, uint8_t object)
{
    return (uint8_t)(dev->base_addr_7bit |
                     ((dev->channel & 1U) << 1) |
                     (object & 1U));
}

static WK2132_Result_t note_hal(WK2132_t *dev, int status)
{
    if (status == 0) {
        return WK2132_OK;
    }
    dev->i2c_errors++;
    return WK2132_I2C_ERROR;
}

static WK2132_Result_t reg_read(WK2132_t *dev, uint8_t reg, uint8_t *data,
                                uint16_t length)
{
    return note_hal(dev, dev->bus->mem_read(dev->bus->ctx,
                    hal_addr(dev, WK2132_OBJECT_REGISTER), reg, data, length));
}

static WK2132_Result_t reg_write(WK2132_t *dev, uint8_t reg,
                                 const uint8_t *data, uint16_t length)
{
    return note_hal(dev, dev->bus->mem_write(dev->bus->ctx,
                    hal_addr(dev, WK2132_OBJECT_REGISTER), reg, data, length));
}

static WK2132_Result_t reg_update(WK2132_t *dev, uint8_t reg,
                                  uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;
    WK2132_Result_t result = reg_read(dev, reg, &value, 1U);
    if (result != WK2132_OK) {
        return result;
    }
    value = (uint8_t)((value & (uint8_t)~clear_mask) | set_mask);
    return reg_write(dev, reg, &value, 1U);
}

static WK2132_Result_t set_page(WK2132_t *dev, uint8_t page)
{
    return reg_update(dev, WK2132_REG_SPAGE, 0x01U, page ? 0x01U : 0x00U);
}

WK2132_Result_t WK2132_Init(WK2132_t *dev, const wk2132_i2c_t *bus,
                            uint8_t channel, bool ia1, bool ia0)
{
    if ((dev == NULL) || (bus == NULL) || (channel > 1U)) {
        return WK2132_BAD_ARG;
    }
    dev->bus = bus;
    dev->base_addr_7bit = (uint8_t)((ia1 ? 0x40U : 0U) |
                                    (ia0 ? 0x20U : 0U) |
                                    WK2132_ADDR_FIXED);
    dev->channel = channel;
    dev->i2c_errors = 0U;
    return WK2132_OK;
}

WK2132_Result_t WK2132_Begin4800_8N1(WK2132_t *dev)
{
    uint8_t value;
    uint16_t divisor;
    WK2132_Result_t result;
    if ((dev == NULL) || (dev->bus == NULL)) {
        return WK2132_BAD_ARG;
    }

    /* DFRobot uses GENA bit 7 as a presence/sanity check. */
    result = reg_read(dev, WK2132_REG_GENA, &value, 1U);
    if (result != WK2132_OK) {
        return result;
    }
    if ((value & 0x80U) == 0U) {
        return WK2132_NOT_DETECTED;
    }

    /* Enable channel clock and issue channel reset (channel bits are 0/1). */
    result = reg_update(dev, WK2132_REG_GENA, 0U,
                        (uint8_t)(1U << dev->channel));
    if (result != WK2132_OK) return result;
    result = reg_update(dev, WK2132_REG_GRST, 0U,
                        (uint8_t)(1U << dev->channel));
    if (result != WK2132_OK) return result;

    result = set_page(dev, 0U);
    if (result != WK2132_OK) return result;

    /* Disable UART while changing divisor. */
    value = 0x00U;
    result = reg_write(dev, WK2132_REG_SCR, &value, 1U);
    if (result != WK2132_OK) return result;

    /* Exact for 14.7456 MHz / (16 * 4800): divisor register = 191. */
    divisor = (uint16_t)((WK2132_XTAL_HZ /
                         (16UL * WK2132_UART_BAUD)) - 1UL);
    result = set_page(dev, 1U);
    if (result != WK2132_OK) return result;
    value = (uint8_t)(divisor >> 8);
    result = reg_write(dev, WK2132_REG_BAUD1, &value, 1U);
    if (result != WK2132_OK) return result;
    value = (uint8_t)divisor;
    result = reg_write(dev, WK2132_REG_BAUD0, &value, 1U);
    if (result != WK2132_OK) return result;
    value = 0x00U; /* No fractional prescaler needed at 4800. */
    result = reg_write(dev, WK2132_REG_PRES, &value, 1U);
    if (result != WK2132_OK) return result;

    result = set_page(dev, 0U);
    if (result != WK2132_OK) return result;
    value = 0x00U; /* Normal UART, 8-N-1, no break, no IrDA. */
    result = reg_write(dev, WK2132_REG_LCR, &value, 1U);
    if (result != WK2132_OK) return result;
    value = 0x0DU; /* RX FIFO reset, RX/TX FIFO enabled. */
    result = reg_write(dev, WK2132_REG_FCR, &value, 1U);
    if (result != WK2132_OK) return result;
    value = 0x00U; /* Polling implementation; interrupts not used. */
    result = reg_write(dev, WK2132_REG_SIER, &value, 1U);
    if (result != WK2132_OK) return result;
    value = 0x03U; /* RX and TX enabled, sleep disabled. */
    return reg_write(dev, WK2132_REG_SCR, &value, 1U);
}

WK2132_Result_t WK2132_Write(WK2132_t *dev, const uint8_t *data, size_t length)
{
    uint8_t fsr;
    if ((dev == NULL) || (data == NULL) || (length == 0U) ||
        (length > WK2132_MAX_FIFO)) {
        return WK2132_BAD_ARG;
    }
    WK2132_Result_t result = reg_read(dev, WK2132_REG_FSR, &fsr, 1U);
    if (result != WK2132_OK) return result;
    if ((fsr & WK2132_FSR_TX_FULL) != 0U) return WK2132_FIFO_FULL;

    return note_hal(dev, dev->bus->tx(dev->bus->ctx,
                    hal_addr(dev, WK2132_OBJECT_FIFO), data, (uint16_t)length));
}

WK2132_Result_t WK2132_RxAvailable(WK2132_t *dev, uint16_t *count)
{
    uint8_t raw_count;
    uint8_t fsr;
    if ((dev == NULL) || (count == NULL)) return WK2132_BAD_ARG;
    WK2132_Result_t result = reg_read(dev, WK2132_REG_RFCNT, &raw_count, 1U);
    if (result != WK2132_OK) return result;
    *count = raw_count;
    if (raw_count == 0U) {
        result = reg_read(dev, WK2132_REG_FSR, &fsr, 1U);
        if (result != WK2132_OK) return result;
        if ((fsr & WK2132_FSR_RX_DATA) != 0U) *count = 256U;
    }
    return WK2132_OK;
}

WK2132_Result_t WK2132_Read(WK2132_t *dev, uint8_t *data, size_t capacity,
                            size_t *read_count)
{
    uint16_t available;
    if ((dev == NULL) || (data == NULL) || (read_count == NULL) ||
        (capacity == 0U)) return WK2132_BAD_ARG;
    *read_count = 0U;
    WK2132_Result_t result = WK2132_RxAvailable(dev, &available);
    if (result != WK2132_OK) return result;
    if (available == 0U) return WK2132_OK;
    if (available > capacity) available = (uint16_t)capacity;
    result = note_hal(dev, dev->bus->rx(dev->bus->ctx,
                      hal_addr(dev, WK2132_OBJECT_FIFO), data, available));
    if (result == WK2132_OK) *read_count = available;
    return result;
}

WK2132_Result_t WK2132_FlushRx(WK2132_t *dev)
{
    uint8_t scratch[32];
    size_t count;
    uint16_t available;
    WK2132_Result_t result;
    do {
        result = WK2132_RxAvailable(dev, &available);
        if (result != WK2132_OK) return result;
        if (available == 0U) return WK2132_OK;
        result = WK2132_Read(dev, scratch, sizeof(scratch), &count);
        if (result != WK2132_OK) return result;
    } while (count != 0U);
    return WK2132_OK;
}
