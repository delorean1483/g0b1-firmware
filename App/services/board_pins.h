#ifndef BOARD_PINS_H
#define BOARD_PINS_H
/* Logical output/input identifiers (spec §4). Physical pin/active-level mapping
   lives in the concrete bsp_io HAL (deferred to bench). */
typedef enum {
    OUT_FUEL_PUMP = 0, OUT_STARTER, OUT_GLOW_PLUG, OUT_COMPRESSOR_CLUTCH,
    OUT_HEAT_REVERSER, OUT_EVAP_FAN, OUT_CONDENSER_FAN, OUT_COUNT
} bsp_out_t;
typedef enum { PWM_EVAP_FAN = 0, PWM_CONDENSER_FAN, PWM_COUNT } bsp_pwm_ch_t;
typedef enum { IN_OIL_PRESSURE = 0, IN_TRUCK_IGNITION, IN_COUNT } bsp_in_t;
#endif /* BOARD_PINS_H */
