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
#define IS31_NORMAL_200HZ   0x01u
#define IS31_NORMAL_400HZ   0x05u
#define IS31_CONFIG_VAL     0x00u
#define IS31_CURRENT_VAL    0x3Fu
#define IS31_SCALING_FULL   0xFFu
#define IS31_PWM_MAX        4095u

#define IS31_READ_FAILED 0x100u
#define IS31_NOT_READ    0x101u
typedef struct {
    uint16_t control, before_control, before_mode, after_mode, result;
    uint16_t disabled_mode, cleared_mode, current_mode, pwm_mode;
} is31_enable_sample_t;

uint16_t is31_read_byte(const i2c_backend_t *bus, uint8_t reg);
int is31_init(const i2c_backend_t *bus, uint16_t *reset_regs, uint16_t *configured_regs);     /* Initializes in shutdown. Stops at the first failed transfer. */
/* Reassert PWM mode, write PWM (clamped to 4095), and latch. */
int is31_write_pwm_and_latch(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3);
int is31_set_pwm(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3);
int is31_enable_outputs(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3,
                       is31_enable_sample_t *sample);
void is31_set_pwm_hz(uint16_t hz);
uint16_t is31_pwm_hz(void);
uint8_t is31_control_value(uint16_t out1, uint16_t out2, uint16_t out3);
/* Re-writes configuration and PWM without the reset / shutdown / zero-PWM steps
   of is31_init, so a lit lamp does not flicker. Order: 0x02, 0x03, 0x10, 0x27,
   PWM at 0x19, 0x28, 0x01. From shutdown, wake with 0x05 before setting
   mode or PWM, then verify mode after final enable. Zero requests assert
   shutdown first and restore the full image with outputs off.
   0 ok; -1 at the first failed transfer. */
int is31_restore(const i2c_backend_t *bus, uint16_t out1, uint16_t out2, uint16_t out3,
                 is31_enable_sample_t *sample);
int is31_shutdown(const i2c_backend_t *bus); /* Shutdown first, then 0x02 <- 0. */
#endif
