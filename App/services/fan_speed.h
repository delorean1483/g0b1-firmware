#ifndef FAN_SPEED_H
#define FAN_SPEED_H
#include "types.h"
/* Evap-fan speed → PWM duty (permille), preserving PIC 7/12/22 ms of 22 ms. */
typedef enum { FAN_LOW = 0, FAN_MEDIUM, FAN_HIGH } fan_speed_t;
#define FAN_DUTY_LOW    318u   /* round(7000/22)  */
#define FAN_DUTY_MEDIUM 545u   /* round(12000/22) */
#define FAN_DUTY_HIGH   1000u
uint16_t fan_speed_permille(fan_speed_t s);
#endif /* FAN_SPEED_H */
