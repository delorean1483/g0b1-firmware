#include "fake_wk2132_i2c.h"
#include <string.h>

/* Global (non-paged) registers: GENA(0x00), GRST(0x01), SPAGE(0x03). */
#define REG_GENA   0x00U
#define REG_GRST   0x01U
#define REG_SPAGE  0x03U
/* Status registers: modeled dynamically off the RX-injection state rather
   than the flat register banks below, matching the real WK2132's live
   FIFO-count/status behavior. */
#define REG_RFCNT  0x0AU
#define REG_FSR    0x0BU
#define FSR_TX_FULL (1U << 1)
#define FSR_RX_DATA (1U << 3)

#define REG_BANK_SIZE 16U
#define TX_CAP_SIZE   512U
#define RX_BUF_SIZE   512U

static uint8_t s_global[REG_BANK_SIZE];
static uint8_t s_page0[REG_BANK_SIZE];
static uint8_t s_page1[REG_BANK_SIZE];
static uint8_t s_spage;

static uint8_t s_tx[TX_CAP_SIZE];
static size_t  s_tx_len;

static uint8_t s_rx[RX_BUF_SIZE];
static size_t  s_rx_len;
static size_t  s_rx_pos;

static int is_global_reg(uint8_t reg)
{
    return (reg == REG_GENA) || (reg == REG_GRST) || (reg == REG_SPAGE);
}

static int fw_mem_read(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf,
                        uint16_t len)
{
    (void)ctx;
    (void)addr7;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t r = (uint8_t)(reg + i);
        if (r == REG_RFCNT) {
            size_t remaining = s_rx_len - s_rx_pos;
            buf[i] = (uint8_t)remaining; /* wraps to 0 at exactly 256, like real HW */
        } else if (r == REG_FSR) {
            size_t remaining = s_rx_len - s_rx_pos;
            uint8_t v = 0U; /* TX FIFO never modeled as full */
            if (remaining > 0U) {
                v |= FSR_RX_DATA;
            }
            buf[i] = v;
        } else if (is_global_reg(r)) {
            buf[i] = s_global[r];
        } else {
            buf[i] = s_spage ? s_page1[r] : s_page0[r];
        }
    }
    return 0;
}

static int fw_mem_write(void *ctx, uint8_t addr7, uint8_t reg,
                         const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    (void)addr7;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t r = (uint8_t)(reg + i);
        uint8_t v = buf[i];
        if (r == REG_RFCNT || r == REG_FSR) {
            /* read-only status registers on real silicon */
            continue;
        }
        if (is_global_reg(r)) {
            s_global[r] = v;
            if (r == REG_SPAGE) {
                s_spage = (uint8_t)(v & 0x01U);
            }
        } else if (s_spage) {
            s_page1[r] = v;
        } else {
            s_page0[r] = v;
        }
    }
    return 0;
}

static int fw_tx(void *ctx, uint8_t addr7, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    (void)addr7;
    for (uint16_t i = 0; (i < len) && (s_tx_len < sizeof s_tx); i++) {
        s_tx[s_tx_len++] = buf[i];
    }
    return 0;
}

static int fw_rx(void *ctx, uint8_t addr7, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    (void)addr7;
    size_t remaining = s_rx_len - s_rx_pos;
    size_t n = ((size_t)len < remaining) ? (size_t)len : remaining;
    for (size_t i = 0; i < n; i++) {
        buf[i] = s_rx[s_rx_pos + i];
    }
    s_rx_pos += n;
    return 0;
}

static const wk2132_i2c_t s_bus = {
    .mem_read  = fw_mem_read,
    .mem_write = fw_mem_write,
    .tx        = fw_tx,
    .rx        = fw_rx,
    .ctx       = 0,
};

void fake_wk2132_reset(void)
{
    memset(s_global, 0, sizeof s_global);
    memset(s_page0, 0, sizeof s_page0);
    memset(s_page1, 0, sizeof s_page1);
    s_spage = 0U;
    s_global[REG_GENA] = 0x80U; /* presence bit set by default */
    fake_wk2132_reset_tx();
    memset(s_rx, 0, sizeof s_rx);
    s_rx_len = 0U;
    s_rx_pos = 0U;
}

void fake_wk2132_reset_tx(void)
{
    memset(s_tx, 0, sizeof s_tx);
    s_tx_len = 0U;
}

void fake_wk2132_set_gena_present(bool present)
{
    if (present) {
        s_global[REG_GENA] |= 0x80U;
    } else {
        s_global[REG_GENA] &= (uint8_t)~0x80U;
    }
}

void fake_wk2132_inject_rx(const uint8_t *data, size_t len)
{
    if (len > sizeof s_rx) {
        len = sizeof s_rx;
    }
    memcpy(s_rx, data, len);
    s_rx_len = len;
    s_rx_pos = 0U;
}

size_t fake_wk2132_tx_len(void)
{
    return s_tx_len;
}

const uint8_t *fake_wk2132_tx_data(void)
{
    return s_tx;
}

uint16_t fake_wk2132_divisor(void)
{
    return (uint16_t)(((uint16_t)s_page1[0x04U] << 8) | s_page1[0x05U]);
}

const wk2132_i2c_t *fake_wk2132_i2c(void)
{
    return &s_bus;
}
