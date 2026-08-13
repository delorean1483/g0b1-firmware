#include "fan_speed.h"

uint16_t fan_speed_permille(fan_speed_t s) {
    switch (s) {
        case FAN_LOW:    return FAN_DUTY_LOW;
        case FAN_MEDIUM: return FAN_DUTY_MEDIUM;
        case FAN_HIGH:   return FAN_DUTY_HIGH;
        default:         return FAN_DUTY_HIGH;
    }
}
