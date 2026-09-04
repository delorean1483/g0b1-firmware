#ifndef FAKE_WK2132_I2C_H
#define FAKE_WK2132_I2C_H
#include "wk2132.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* In-memory model of one WK2132: a 256-byte register file per page (SPAGE
   selects page 0 vs page 1 for the BAUD/PRES regs), a TX-capture buffer, and
   an injectable RX buffer. GENA defaults to 0x80 (presence bit set) so
   WK2132_Begin4800_8N1's detection check passes out of the box. */
void fake_wk2132_reset(void);                 /* zero regs, GENA=0x80, clear tx/rx */
void fake_wk2132_reset_tx(void);              /* clear only the TX-capture buffer */
void fake_wk2132_set_gena_present(bool present); /* set/clear GENA bit 7 */
void fake_wk2132_inject_rx(const uint8_t *data, size_t len); /* queue RX bytes */

size_t         fake_wk2132_tx_len(void);      /* bytes captured via bus->tx */
const uint8_t *fake_wk2132_tx_data(void);     /* backing TX-capture buffer */
uint16_t       fake_wk2132_divisor(void);     /* last BAUD1:BAUD0 written on page 1 */

const wk2132_i2c_t *fake_wk2132_i2c(void);    /* backend vtable wired to the fake */

#endif /* FAKE_WK2132_I2C_H */
