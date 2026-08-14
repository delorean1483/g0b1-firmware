#ifndef CONTROL_H
#define CONTROL_H
#include "types.h"
#include "fan_speed.h"   /* fan_speed_t for the evap-fan request */

#define CONTROL_INPUT_DEBOUNCE_TIME 50u   /* PIC oil/ignition: 50 x 10 ms = 500 ms */

typedef enum {
    OP_POWER_UP = 0, OP_OFF, OP_ENGINE_START, OP_CLIMATE, OP_BATTERY,
    OP_COLD_STORAGE, OP_ERROR_SHUTDOWN, OP_STATE_COUNT
} control_op_state_t;

typedef enum {
    ST_OFF = 0, ST_WARMING_UP, ST_STARTING, ST_RUNNING, ST_DEFROST,
    ST_CHARGING, ST_COOLING, ST_CHILLIN
} control_status_t;

typedef enum { MODE_OFF = 0, MODE_CLIMATE, MODE_BATTERY } op_mode_t;

typedef enum {
    ERR_NONE = 0, ERR_LOW_OIL, ERR_HIGH_ENGINE_TEMP, ERR_LOW_BATTERY,
    ERR_AC_LOW_PRESSURE, ERR_AC_HIGH_PRESSURE, ERR_STARTING_FAILURE, ERR_STANDBY
} control_error_t;

typedef enum {
    OIL_GOOD = 0, OIL_CHANGE_SOON, OIL_CHANGE_NEEDED, OIL_CHANGE_PAST_DUE, OIL_WARNING_DISMISSED
} oil_state_t;

typedef enum { TD_REAL_TIME = 0, TD_CC_SETTING, TD_CS_SETTING } temp_display_t;

/* Output *requests* the modes set; outputs_apply() maps them to bsp_io/bsp_pwm. */
typedef struct {
    bool fuel_pump, starter, glow_plug, compressor_clutch, heat_reverse;
    bool evap_fan;          fan_speed_t evap_speed;
    bool condenser_fan;     uint16_t condenser_duty;   /* permille */
} apu_outputs_t;

typedef struct {
    control_op_state_t op_state;
    uint8_t            sub_state;         /* per-mode state (PIC `state`) */
    apu_outputs_t      out;               /* requested outputs */
    uint8_t            engine_op_status;  /* control_status_t */
    uint8_t            control_status;    /* control_status_t */
    uint8_t            error_state;       /* control_error_t */
    uint8_t            oil_change_state;  /* oil_state_t */
    uint8_t            temp_display_state;/* temp_display_t */
    uint8_t            mode_request;      /* op_mode_t (Modbus reg 10) */
    bool               in_oil_pressure_ok;/* debounced oil pressure */
    bool               in_truck_ignition; /* debounced truck ignition */
    bool               evap_fan_always_on;/* flag2 equivalent */
    /* --- M6b engine-start --- */
    uint8_t  op_state_previous;        /* control_op_state_t: mode that invoked engine start */
    uint8_t  attempted_start_counter;  /* start attempts this cycle */
    int16_t  external_temperature;     /* degF, from M3 sensors (glow-plug timing) */
    uint8_t  ext_temp_sensor_state;    /* SENSOR_ON/OFF from M3 */
    bool     engine_temp_ok;           /* false => over-temp fault (sensor derivation deferred; default true) */
    bool     standby_override;         /* Modbus reg 32; when true, suppress standby shutdown */
} apu_ctx_t;

typedef void (*control_mode_fn)(apu_ctx_t *ctx);

void control_init(apu_ctx_t *ctx);
void control_register_mode(control_op_state_t st, control_mode_fn fn);
void control_tick(apu_ctx_t *ctx);   /* apply mode-request transition, then dispatch */
void outputs_apply(const apu_ctx_t *ctx);
void control_powerup_mode(apu_ctx_t *ctx);   /* register for OP_POWER_UP */
void control_off_mode(apu_ctx_t *ctx);   /* register for OP_OFF */
void control_inputs_init(apu_ctx_t *ctx);
void control_inputs_service(apu_ctx_t *ctx);
void control_sample_sensors(apu_ctx_t *ctx);
void control_regs_register(apu_ctx_t *ctx);

/* app-global control context and wiring */
apu_ctx_t *control_app_ctx(void);
void       control_app_init(void);
void       control_10ms_slot(void);

#endif /* CONTROL_H */
