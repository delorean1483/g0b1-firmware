#ifndef HTR_BOARD_H
#define HTR_BOARD_H
/* EF-L011 board wiring. THE ONLY FILE THAT ENCODES THE SCHEMATIC.
   Every value below was read from the R0.00.00 PDF and MUST be confirmed
   against the finished schematic (spec section 2 / open item 1) before
   bench step M2. Alternate-function numbers come from the STM32L011x4
   datasheet pin table (DS11205), not from the schematic. */
#include "stm32l0xx.h"

/* ---- wiring class -> UART (R0.00.00 net labels HTR3v3_3W_* / HTR3v3_4W_*) ---- */
/* 3-wire: LPUART1, RX PA0 / TX PA1, AF6. TX path inverts (Q5 open-drain). */
#define HB_3W_UART            LPUART1
#define HB_3W_IRQn            LPUART1_IRQn
#define HB_3W_RX_PORT         GPIOA
#define HB_3W_RX_PIN          0u
#define HB_3W_TX_PORT         GPIOA
#define HB_3W_TX_PIN          1u
#define HB_3W_AF              6u
#define HB_3W_TX_INVERT       1u
#define HB_3W_RX_INVERT       0u
/* 4-wire: USART2, TX PB6 / RX PB7, AF0. No inversion. */
#define HB_4W_UART            USART2
#define HB_4W_IRQn            USART2_IRQn
#define HB_4W_RX_PORT         GPIOB
#define HB_4W_RX_PIN          7u
#define HB_4W_TX_PORT         GPIOB
#define HB_4W_TX_PIN          6u
#define HB_4W_AF              0u
#define HB_4W_TX_INVERT       0u
#define HB_4W_RX_INVERT       0u

/* ---- control lines: pin + level that means "active" (htr_iface semantics) ---- */
/* HTR_TX_MODE PA6 -> TMUX1072 SEL. active = 4-wire path (NO1). R0 reading: SEL high = NO. */
#define HB_TX_MODE_PORT       GPIOA
#define HB_TX_MODE_PIN        6u
#define HB_TX_MODE_ACTIVE_HI  1u
/* HTR_TX_nEN PA7 -> TMUX1072 OE (active low). active = connected. */
#define HB_TX_EN_PORT         GPIOA
#define HB_TX_EN_PIN          7u
#define HB_TX_EN_ACTIVE_HI    0u
/* HTR_TX_PWR / HTR_5V_EN PB1 -> XC6705 CE. active = +5V_HTR on. */
#define HB_TX_PWR_PORT        GPIOB
#define HB_TX_PWR_PIN         1u
#define HB_TX_PWR_ACTIVE_HI   1u
/* Does the 3-wire path need +5V_HTR? R0 reading: no (open-drain to GND). */
#define HB_PWR_FOR_3W         0u

/* ---- GPIOX6 / GPIOX7 (PA4 / PA5): test points on R0. Left analog (hi-Z)
   unless HB_GPIOX_DEBUG=1 and the schematic confirms they are unloaded. ---- */
#define HB_GPIOX6_PORT        GPIOA
#define HB_GPIOX6_PIN         4u
#define HB_GPIOX7_PORT        GPIOA
#define HB_GPIOX7_PIN         5u
#ifndef HB_GPIOX_DEBUG
#define HB_GPIOX_DEBUG        0u
#endif

/* ---- I2C1 slave to the G0B1: SCL PA9 / SDA PA10, AF1 ---- */
#define HB_I2C_SCL_PIN        9u
#define HB_I2C_SDA_PIN        10u
#define HB_I2C_AF             1u
/* I2C1 kernel clock = HSI16. 100 kHz standard-mode timing (RM0377 table). */
#define HB_I2C_TIMINGR        0x00303D5Bu

#define HB_HW_ID              1u   /* HTR_HW_ID_EF_L011 */
#endif
