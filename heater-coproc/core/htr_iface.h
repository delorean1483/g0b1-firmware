#ifndef HTR_IFACE_H
#define HTR_IFACE_H
/* Heater TX routing / power sequencing. All pin levels live in the board
   layer (port/<mcu>/htr_board.h): this module only speaks in semantic
   "active" terms, so a schematic change never touches core code. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HTR_LINE_TX_MODE = 0,   /* active = 4-wire TX path selected            */
    HTR_LINE_TX_EN,         /* active = TX path connected to the heater wire */
    HTR_LINE_TX_PWR,        /* active = TX stage supply on                  */
    HTR_LINE_COUNT
} htr_line_t;

typedef struct htr_gpio {
    void (*set_line)(void *ctx, htr_line_t line, bool active);
    /* attach=true: UART pins of that wiring to their peripheral function;
       false: plain inputs (TX released, RX readable as GPIO). */
    void (*attach_uart)(void *ctx, uint8_t wiring, bool attach);
    /* Current level of the RX comparator output for that wiring (true = high). */
    bool (*rx_level)(void *ctx, uint8_t wiring);
    void *ctx;
} htr_gpio_t;

#define HTR_IFACE_SETTLE_MS 5u

void    htr_iface_init(const htr_gpio_t *g, bool pwr_for_3w); /* safe state */
void    htr_iface_enable(uint8_t wiring, uint32_t now);       /* starts the sequence */
void    htr_iface_disable(void);
void    htr_iface_process(uint32_t now);
bool    htr_iface_ready(void);
uint8_t htr_iface_wiring(void);                               /* HTR_WIRING_NONE if off */
bool    htr_iface_rx_level(uint8_t wiring);
#endif
