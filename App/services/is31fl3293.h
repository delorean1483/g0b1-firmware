#ifndef IS31FL3293_H
#define IS31FL3293_H
/* IS31FL3293 3-channel LED driver (external RGB lamp), register-level driver
   over the i2c_backend_t seam. No HAL, no blocking waits. */
#include "i2c_backend.h"
#include <stdint.h>

#define IS31_ADDR7          0x6Cu

#define IS31_REG_SHUTDOWN   0x01u
#define IS31_REG_CONFIG     0x02u
#define IS31_REG_CURRENT    0x03u
#define IS31_REG_SCALING    0x10u   /* 0x10..0x12, one per output */
#define IS31_REG_PWM        0x19u   /* 0x19..0x1E, low byte then high nibble per output */
#define IS31_REG_CL_UPDATE  0x27u   /* latches current/scaling (color) settings */
#define IS31_REG_PWM_UPDATE 0x28u   /* latches the PWM registers */
#define IS31_REG_RESET      0x3Fu

#define IS31_KEY            0xC5u
#define IS31_SHUTDOWN_OFF   0x70u
#define IS31_NORMAL_ON      0x75u
#define IS31_CONFIG_VAL     0x00u
#define IS31_CURRENT_VAL    0x3Fu
#define IS31_SCALING_FULL   0xFFu
#define IS31_PWM_MAX        4095u

int is31_init(const i2c_backend_t *bus);     /* 0 ok; -1 at the first failed transfer (stops there) */
int is31_set_pwm(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3); /* clamps each to 4095 */
/* Re-writes configuration and PWM without the reset / shutdown / zero-PWM steps
   of is31_init, so a lit lamp does not flicker. Order: 0x02, 0x03, 0x10, 0x27,
   PWM at 0x19, 0x28, 0x01. 0 ok; -1 at the first failed transfer. */
int is31_restore(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3);
int is31_shutdown(const i2c_backend_t *bus); /* 0x01 <- 0x70 */
#endif
