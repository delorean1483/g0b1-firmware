/* EF-L011 heater coprocessor: init, then a non-blocking superloop. */
#include "port.h"
#include "htr_board.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_regs.h"
#include "drv_autoterm.h"
#include "drv_vevor.h"
#include "l011_update.h"

static const htr_driver_t *const s_drivers[] = { &drv_vevor, &drv_autoterm };
extern const uint32_t __app_vectors[];

int main(void)
{
    SCB->VTOR = (uint32_t)__app_vectors;
    __DSB();
    __ISB();
    port_clock_init();
    port_gpio_init();                 /* safe levels before anything else */
    port_tick_init();
    port_uart_init();

    htr_regs_cfg_t rc = {
        &port_nv, HB_HW_ID,
        (uint16_t)((1u << HTR_TYPE_VEVOR) | (1u << HTR_TYPE_AUTOTERM)),
        port_lock, port_unlock
    };
    uint8_t type = htr_regs_init(&rc, port_now());

    htr_fsm_cfg_t fc = {
        s_drivers, (uint8_t)(sizeof(s_drivers) / sizeof(s_drivers[0])),
        &port_gpio, HB_PWR_FOR_3W != 0u, &port_uart_3w, &port_uart_4w
    };
    htr_fsm_init(&fc, type, port_now());

    port_i2c_slave_init(HTR_I2C_ADDR7);
    port_iwdg_start();                /* armed after init (boot-loop lesson) */

    for (;;) {
        uint32_t now = port_now();
        port_uart_process(now);
        htr_regs_poll(now);
        htr_fsm_process(now);
#ifdef L011_RELOCATED_APP
        if (port_take_update_request()) {
            htr_fsm_view_t view;
            htr_fsm_view(now, &view);
            if (view.phase == HTR_PHASE_OFF && !view.rq.run) {
                __disable_irq();
                volatile uint32_t *const request = (volatile uint32_t *)L011_RESET_ADDR;
                request[0] = L011_RESET_MAGIC;
                request[1] = ~L011_RESET_MAGIC;
                NVIC_SystemReset();
            }
        }
#endif
        htr_regs_publish(now, port_uart_echo_errs());
        port_iwdg_kick();
    }
}
