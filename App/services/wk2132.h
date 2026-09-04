#ifndef WK2132_H
#define WK2132_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Backend seam: the four I2C ops the WK2132 needs. Each returns 0 on
   success, non-zero on I2C error. addr7 is the 7-bit device address the
   driver computes (channel/object encoded); the on-target backend shifts
   it for HAL, the fake decodes it directly.

   The WK2132 needs BOTH register (Mem) access (SPAGE/SCR/LCR/FCR/...) AND
   raw FIFO (Master Tx/Rx) access, unlike the project's register-only
   i2c_backend_t (which fits the RTC) -- hence this dedicated backend. */
typedef struct wk2132_i2c {
    int (*mem_read) (void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf, uint16_t len);
    int (*mem_write)(void *ctx, uint8_t addr7, uint8_t reg, const uint8_t *buf, uint16_t len);
    int (*tx)(void *ctx, uint8_t addr7, const uint8_t *buf, uint16_t len);
    int (*rx)(void *ctx, uint8_t addr7, uint8_t *buf, uint16_t len);
    void *ctx;
} wk2132_i2c_t;

typedef enum {
    WK2132_OK = 0,
    WK2132_BAD_ARG,
    WK2132_I2C_ERROR,
    WK2132_NOT_DETECTED,
    WK2132_FIFO_FULL
} WK2132_Result_t;

typedef struct {
    const wk2132_i2c_t *bus;
    uint8_t  base_addr_7bit;
    uint8_t  channel;
    uint32_t i2c_errors;
} WK2132_t;

/* ia1/ia0 reflect the DFR0627 DIP switches; channel is 0 or 1. */
WK2132_Result_t WK2132_Init(WK2132_t *dev, const wk2132_i2c_t *bus,
                            uint8_t channel, bool ia1, bool ia0);
WK2132_Result_t WK2132_Begin4800_8N1(WK2132_t *dev);
WK2132_Result_t WK2132_Write(WK2132_t *dev, const uint8_t *data, size_t length);
WK2132_Result_t WK2132_RxAvailable(WK2132_t *dev, uint16_t *count);
WK2132_Result_t WK2132_Read(WK2132_t *dev, uint8_t *data, size_t capacity,
                            size_t *read_count);
WK2132_Result_t WK2132_FlushRx(WK2132_t *dev);

#endif /* WK2132_H */
