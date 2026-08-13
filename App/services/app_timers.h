#ifndef APP_TIMERS_H
#define APP_TIMERS_H
#include "types.h"

typedef enum { SCALE_MS = 0, SCALE_TEN_MS, SCALE_HUNDRED_MS, SCALE_SECOND, SCALE_MINUTE, SCALE_COUNT } app_timer_scale_t;

enum { EVAP_PWM_PERIOD_TMR = 0, EVAP_PWM_ON_TMR, NUM_ONE_MS_TIMER };
enum { SHORT_DELAY_TMR = 0, RPM_STOP_TMR, NUM_TEN_MS_TIMER };
enum { GLOW_PLUG_ON_TMR = 0, RPM_LOW_TMR, NUM_100_MS_TIMER };
enum { POWER_UP_TMR = 0, EVENT_INTERVAL_TMR, COMP_EVAP_DELAY_TMR, BATT_STABLE_TMR,
       COMPRESOR_OUT_TMR, FUEL_PUMP_ONOFF_TIMER, EVAP_FORCED_ON_TMR, NUM_ONE_SECOND_TIMER };
enum { CHARGING_BATT_TMR = 0, NEXT_OIL_WARNING_TMR, DEFROST_CYCLE_TMR, CLMT_LOW_BATT_TMR,
       CABIN_TEMP_WARMUP_TMR, NUM_ONE_MINUTE_TIMER };

void     app_timers_init(void);
void     app_timer_set(app_timer_scale_t s, uint8_t idx, uint16_t ticks);
uint16_t app_timer_get(app_timer_scale_t s, uint8_t idx);
bool     app_timer_expired(app_timer_scale_t s, uint8_t idx);   /* value == 0 */
void     app_timers_tick(app_timer_scale_t s);                  /* decrement nonzero timers in scale s */

#endif /* APP_TIMERS_H */
