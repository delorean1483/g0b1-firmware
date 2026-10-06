#ifndef HTR_FSM_H
#define HTR_FSM_H
/* Generic heater state machine + auto-detection (spec section 4). Owns the
   active driver and the interface; knows nothing about I2C or registers. */
#include "htr_driver.h"
#include "htr_iface.h"
#include "htr_types.h"

#define HTR_BOOT_QUIET_MS      3000u
#define HTR_SENSE_MS           1000u
#define HTR_SENSE_SAMPLE_MS      10u
#define HTR_SENSE_HIGH_PCT       90u   /* >= this % high = idle-high      */
#define HTR_SENSE_LOW_PCT        10u   /* <= this % high = idle-low       */
#define HTR_DETECT_RETRY_MS   10000u
#define HTR_FRESH_MS           3000u
#define HTR_HOTSWAP_MS        30000u
#define HTR_START_TIMEOUT_MS  (10u * 60u * 1000u)
#define HTR_COOLDOWN_TIMEOUT_MS (15u * 60u * 1000u)
#define HTR_STOP_SETTLE_MS     1500u   /* standby report must be this much newer than the stop */

typedef struct {
    const htr_driver_t *const *drivers;
    uint8_t n_drivers;
    const htr_gpio_t *gpio;
    bool pwr_for_3w;
    const htr_uart_t *uart_3w;
    const htr_uart_t *uart_4w;
} htr_fsm_cfg_t;

typedef struct {
    uint8_t  phase;          /* htr_phase_t */
    uint8_t  active_type;    /* HTR_TYPE_* or NONE */
    uint8_t  line_sense;     /* HTR_LS_* */
    uint8_t  detect_result;  /* HTR_DET_* */
    uint16_t flags;          /* HTR_F_* owned by the FSM (not HOST_TIMEOUT/ECHO) */
    uint16_t caps;
    htr_request_t rq;        /* effective request (run is forced 0 in FAULT) */
    htr_status_t st;
} htr_fsm_view_t;

/* type_setting: HTR_TYPE_AUTO, HTR_TYPE_NONE, or a forced type id. */
void htr_fsm_init(const htr_fsm_cfg_t *cfg, uint8_t type_setting, uint32_t now);
void htr_fsm_process(uint32_t now);
/* Level-triggered: call with the full request whenever any field changes. */
void htr_fsm_set_request(const htr_request_t *rq);
/* Return HTR_RES_* codes (htr_i2c_map.h). */
uint8_t htr_fsm_set_type(uint8_t type_setting, uint32_t now);
uint8_t htr_fsm_clear_fault(uint32_t now);
uint8_t htr_fsm_redetect(uint32_t now);
uint8_t htr_fsm_reinit_iface(uint32_t now);
void htr_fsm_view(uint32_t now, htr_fsm_view_t *out);
#endif
