#ifndef HTR_REGS_H
#define HTR_REGS_H
/* I2C register file (spec section 6). The I2C slave ISR calls the
   htr_regs_i2c_* functions; the superloop calls htr_regs_poll() (apply
   writes/commands, host timeout) and htr_regs_publish() (refresh status).
   Shared state is guarded by the lock()/unlock() hooks (IRQ off on target). */
#include "htr_nv.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const htr_nv_t *nv;
    uint8_t hw_id;
    uint16_t supported_types;
    void (*lock)(void);
    void (*unlock)(void);
} htr_regs_cfg_t;

/* Loads persisted settings. Returns the HEATER_TYPE setting to boot with. */
uint8_t htr_regs_init(const htr_regs_cfg_t *cfg, uint32_t now);
uint8_t htr_regs_host_timeout_min(void);

/* ---- ISR side ---- */
void    htr_regs_i2c_begin(bool is_read);   /* address match */
void    htr_regs_i2c_rx(uint8_t byte);      /* master wrote a byte */
uint8_t htr_regs_i2c_tx(void);              /* master reads a byte */
void    htr_regs_i2c_end(void);             /* STOP */

/* ---- superloop side ---- */
void htr_regs_poll(uint32_t now);
void htr_regs_publish(uint32_t now, uint32_t echo_errs);
#endif
