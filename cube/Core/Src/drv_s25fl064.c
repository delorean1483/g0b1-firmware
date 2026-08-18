/* drv_s25fl064.c — SPI2 NOR implementation of nvm_backend_t (Task 6).
 *
 * S25FL064: 8 MByte, 4 KB sectors (SECTOR_ERASE 0x20), 256 B program pages.
 * Wiring: SPI2 (PD1 SCK / PD3 MISO / PD4 MOSI), software CS on PC2 (active low).
 *
 * program() is the silent-corruption hazard: a PAGE PROGRAM wraps within a
 * 256-byte page, and the NVM journal writes 264-byte records — so every write is
 * split at page boundaries by nor_page_split() (host-tested in test_nor_page_split).
 *
 * WIP polling is a bounded busy-wait: page-program (~ms) is cheap; a SECTOR ERASE
 * (~50-300 ms) blocks the superloop for its duration. That is inherent to the
 * synchronous nvm_backend_t contract (erase() returns only on completion) and is
 * infrequent (once per ~15 journal records). Accepted for bring-up; the plan
 * flags it as a bench watch item.
 */
#include "drv_s25fl064.h"
#include "nor_page_split.h"
#include "main.h"      /* CubeMX HAL: hspi2, HAL_SPI_*, HAL_GPIO_*, HAL_GetTick */
#include <stddef.h>

extern SPI_HandleTypeDef hspi2;   /* CubeMX-generated (MX_SPI2_Init) */

/* Geometry (S25FL064 = 64 Mbit). The whole chip is the NVM journal region. */
#define S25_SECTOR_SIZE    4096u
#define S25_SECTOR_COUNT   2048u

/* Software chip-select: PC2 (raw, so it is independent of the .ioc User Label). */
#define NOR_CS_PORT        GPIOC
#define NOR_CS_PIN         GPIO_PIN_2
#define CS_LOW()           HAL_GPIO_WritePin(NOR_CS_PORT, NOR_CS_PIN, GPIO_PIN_RESET)
#define CS_HIGH()          HAL_GPIO_WritePin(NOR_CS_PORT, NOR_CS_PIN, GPIO_PIN_SET)

/* Command set */
#define CMD_WREN           0x06u
#define CMD_READ           0x03u   /* read data (24-bit addr) */
#define CMD_PP             0x02u   /* page program (24-bit addr) */
#define CMD_SE             0x20u   /* 4 KB sector erase (24-bit addr) */
#define CMD_RDSR1          0x05u   /* read status register 1 */
#define CMD_RDID           0x9Fu   /* JEDEC ID */
#define SR_WIP             0x01u   /* status reg 1: write-in-progress */

#define SPI_TMO_MS         100u    /* per-transfer HAL timeout */
#define WIP_TMO_PROGRAM    50u     /* page program worst case ~few ms */
#define WIP_TMO_ERASE      1000u   /* sector erase worst case a few hundred ms */

static int spi_tx(const uint8_t *d, uint16_t n)
{ return (HAL_SPI_Transmit(&hspi2, (uint8_t *)d, n, SPI_TMO_MS) == HAL_OK) ? 0 : -1; }

static int spi_rx(uint8_t *d, uint16_t n)
{ return (HAL_SPI_Receive(&hspi2, d, n, SPI_TMO_MS) == HAL_OK) ? 0 : -1; }

static void put_addr(uint8_t hdr[4], uint8_t cmd, uint32_t addr)
{
    hdr[0] = cmd;
    hdr[1] = (uint8_t)(addr >> 16);
    hdr[2] = (uint8_t)(addr >> 8);
    hdr[3] = (uint8_t)(addr);
}

static int write_enable(void)
{
    uint8_t c = CMD_WREN;
    CS_LOW();
    int r = spi_tx(&c, 1u);
    CS_HIGH();
    return r;
}

/* Bounded busy-wait until WIP clears (or timeout). */
static int wait_wip_clear(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    for (;;) {
        uint8_t cmd = CMD_RDSR1, sr = 0xFFu;
        CS_LOW();
        int r = spi_tx(&cmd, 1u);
        if (r == 0) r = spi_rx(&sr, 1u);
        CS_HIGH();
        if (r != 0) return -1;
        if ((sr & SR_WIP) == 0u) return 0;
        if ((HAL_GetTick() - start) > timeout_ms) return -1;
    }
}

static int nor_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    uint8_t hdr[4];
    put_addr(hdr, CMD_READ, addr);
    CS_LOW();
    int r = spi_tx(hdr, 4u);
    if (r == 0 && len > 0u) r = spi_rx(buf, (uint16_t)len);
    CS_HIGH();
    return r;
}

/* One page-bounded chunk from nor_page_split -> WREN + PAGE PROGRAM + WIP poll. */
typedef struct { int err; } pp_ctx_t;

static void pp_chunk(void *vctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    pp_ctx_t *pc = (pp_ctx_t *)vctx;
    if (pc->err != 0) return;                 /* short-circuit after a failure */
    if (write_enable() != 0) { pc->err = -1; return; }
    uint8_t hdr[4];
    put_addr(hdr, CMD_PP, addr);
    CS_LOW();
    int r = spi_tx(hdr, 4u);
    if (r == 0 && len > 0u) r = spi_tx(buf, (uint16_t)len);
    CS_HIGH();
    if (r != 0) { pc->err = -1; return; }
    if (wait_wip_clear(WIP_TMO_PROGRAM) != 0) { pc->err = -1; return; }
}

static int nor_program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    pp_ctx_t pc = { 0 };
    nor_page_split(addr, buf, len, pp_chunk, &pc);   /* split at 256 B pages */
    return pc.err;
}

static int nor_erase(void *ctx, uint32_t sector_index)
{
    (void)ctx;
    if (sector_index >= S25_SECTOR_COUNT) return -1;
    uint8_t hdr[4];
    put_addr(hdr, CMD_SE, sector_index * S25_SECTOR_SIZE);
    if (write_enable() != 0) return -1;
    CS_LOW();
    int r = spi_tx(hdr, 4u);
    CS_HIGH();
    if (r != 0) return -1;
    return wait_wip_clear(WIP_TMO_ERASE);
}

static const nvm_backend_t s_backend = {
    .sector_size  = S25_SECTOR_SIZE,
    .sector_count = S25_SECTOR_COUNT,
    .read         = nor_read,
    .program      = nor_program,
    .erase        = nor_erase,
    .ctx          = NULL,
};

const nvm_backend_t *drv_s25fl064_backend(void) { return &s_backend; }

int drv_s25fl064_read_id(uint8_t id[3])
{
    uint8_t cmd = CMD_RDID;
    CS_LOW();
    int r = spi_tx(&cmd, 1u);
    if (r == 0) r = spi_rx(id, 3u);
    CS_HIGH();
    return r;
}
