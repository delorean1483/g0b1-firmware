#include "htr_iface.h"
#include "htr_types.h"
#include <stddef.h>

typedef enum { IF_OFF = 0, IF_SETTLING, IF_ON } if_state_t;

static const htr_gpio_t *s_g;
static bool s_pwr_for_3w;
static if_state_t s_state;
static uint8_t s_wiring;
static uint32_t s_t0;

static void line(htr_line_t l, bool active) { s_g->set_line(s_g->ctx, l, active); }

static void all_safe(void)
{
    line(HTR_LINE_TX_EN, false);       /* disconnect first */
    s_g->attach_uart(s_g->ctx, HTR_WIRING_3W, false);
    s_g->attach_uart(s_g->ctx, HTR_WIRING_4W, false);
    line(HTR_LINE_TX_PWR, false);
    line(HTR_LINE_TX_MODE, false);
}

void htr_iface_init(const htr_gpio_t *g, bool pwr_for_3w)
{
    s_g = g;
    s_pwr_for_3w = pwr_for_3w;
    s_state = IF_OFF;
    s_wiring = HTR_WIRING_NONE;
    all_safe();
}

void htr_iface_enable(uint8_t wiring, uint32_t now)
{
    if ((wiring != HTR_WIRING_3W) && (wiring != HTR_WIRING_4W)) return;
    if ((s_state != IF_OFF) && (s_wiring == wiring)) return;
    if (s_state != IF_OFF) htr_iface_disable();   /* class change goes via off */
    line(HTR_LINE_TX_EN, false);
    line(HTR_LINE_TX_MODE, wiring == HTR_WIRING_4W);
    line(HTR_LINE_TX_PWR, (wiring == HTR_WIRING_4W) || s_pwr_for_3w);
    s_wiring = wiring;
    s_t0 = now;
    s_state = IF_SETTLING;
}

void htr_iface_process(uint32_t now)
{
    if (s_state != IF_SETTLING) return;
    if ((uint32_t)(now - s_t0) < HTR_IFACE_SETTLE_MS) return;
    s_g->attach_uart(s_g->ctx, s_wiring, true);
    line(HTR_LINE_TX_EN, true);
    s_state = IF_ON;
}

void htr_iface_disable(void)
{
    if (s_g == NULL) return;
    all_safe();
    s_state = IF_OFF;
    s_wiring = HTR_WIRING_NONE;
}

bool    htr_iface_ready(void)  { return s_state == IF_ON; }
uint8_t htr_iface_wiring(void) { return s_wiring; }
bool    htr_iface_rx_level(uint8_t wiring) { return s_g->rx_level(s_g->ctx, wiring); }
