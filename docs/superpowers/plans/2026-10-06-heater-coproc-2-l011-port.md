# Heater Coprocessor — Plan 2 of 3: EF-L011 Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a flashable STM32L011F4 firmware image that runs the Plan 1 core on the EF-L011 board (HSI16, LPUART1 + USART2 IRQ rings with 3-wire echo stripping, I2C1 slave at 0x64, data-EEPROM settings, IWDG), within 16 KB flash / 2 KB RAM, plus the bench bring-up guide.

**Architecture:** `heater-coproc/port/l011/` is the only MCU/board-specific code. `htr_board.h` is the single file that encodes the schematic (UART↔wiring map, control-line active levels, AF numbers). Port files talk to registers through the vendored ST CMSIS device header (no HAL, no LL — smallest image; the spec's "LL" intent is met by staying below HAL). Built by a plain Makefile with STM32CubeIDE's bundled `arm-none-eabi` toolchain, mirroring `boot/Makefile`.

**Tech Stack:** C11, arm-none-eabi-gcc 13.3 (CubeIDE), ST `cmsis_device_l0` v1.9.4, CMSIS core from `cube/Drivers/CMSIS/Include`.

**Spec:** `docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md`

**Depends on:** Plan 1 (`2026-10-06-heater-coproc-1-core.md`) fully merged — this plan links `heater-common/*.c` and `heater-coproc/core/*.c`.

## Global Constraints

- Every schematic-derived value lives in `heater-coproc/port/l011/htr_board.h` and nowhere else (spec §2 rule). Values come from R0.00.00 and are re-confirmed against the finished schematic during bench M2 (Task 3 guide).
- Reset/boot: control lines are written to their **inactive** level before their pins become outputs; UART pins stay inputs until `attach_uart`; GPIOX6/X7 stay analog (hi-Z) unless `HB_GPIOX_DEBUG=1`.
- Clocks: HSI16 SYSCLK, 1 flash wait state, APB1 /2 → PCLK1 8 MHz (LPUART1 + USART2 kernel clock); I2C1 kernel clock HSI16.
- 3-wire UART: LPUART1, TXINV per board, local echo stripped and counted (`port_uart_echo_errs()`).
- IWDG armed **after** all init (~1.7 s), frozen in debug only when built with `DEBUG=1`.
- Size gate: hard limit 16384 B flash / 2048 B RAM (incl. 512 B stack); warn above 12288 B flash.
- Build: `-Wall -Wextra -Werror`, zero warnings.
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk
  ```

## Review Focus

These are on-target behaviours host tests cannot reach; each is a named bench check in the Task 3 guide.

1. **Boot glitch on control lines** (a control pin briefly active between reset and init): expect PA6/PA7/PB1 inactive from power-on → guide M2 scope check; code: latch-then-output order in `port_gpio_init()`.
2. **Self-echo on the 3-wire wire mistaken for heater data:** expect `ECHO_ERRS` = 0 with a healthy wire → guide M4.
3. **Baud change while bytes are still shifting out** (AUTOTERM 2400→4800→9600): expect `set_baud` refused until `TC`, so no truncated frame → guide M4 4-wire capture.
4. **Mem_Read with repeated start (no STOP between pointer write and read):** expect correct data and no spurious commit → G0B1 read of `EF 01` at M3; code path `I2C1_IRQHandler` ADDR handling.
5. **EEPROM write stalls the loop** (~3.2 ms per save): only on HEATER_TYPE/HOST_TIMEOUT change, never in the steady loop → M5 (type change) with IWDG running.

## File Structure

| File | Responsibility |
|---|---|
| `heater-coproc/port/l011/vendor/cmsis_device/*` | ST CMSIS device files for STM32L011 (pinned v1.9.4) + licence |
| `heater-coproc/port/l011/l011.ld` | linker script, 16 KB / 2 KB, 512 B stack |
| `heater-coproc/port/l011/Makefile` | build + size gate |
| `heater-coproc/port/l011/htr_board.h` | schematic-derived wiring (single source) |
| `heater-coproc/port/l011/port.h` | port API used by `main.c` |
| `heater-coproc/port/l011/port_sys.c` | clock, SysTick, IWDG, IRQ lock, `_init/_fini` |
| `heater-coproc/port/l011/port_gpio.c` | control lines, line sense, UART pin attach, I2C pins (`htr_gpio_t`) |
| `heater-coproc/port/l011/port_uart.c` | LPUART1/USART2 IRQ rings, echo strip (`htr_uart_t` ×2) |
| `heater-coproc/port/l011/port_i2c_slave.c` | I2C1 slave IRQ → `htr_regs_i2c_*` |
| `heater-coproc/port/l011/port_nv.c` | data-EEPROM settings word (`htr_nv_t`) |
| `heater-coproc/port/l011/main.c` | init order + superloop |
| `heater-coproc/port/l011/.gitignore` | `build/` |
| `docs/heater-coprocessor.md` | build/flash/bench guide |

**Toolchain for every build step:**

```bash
export PATH="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin:$PATH"
```

---

### Task 1: Build skeleton — vendor files, linker, Makefile, board header, clock/GPIO/IWDG

**Files:**
- Create: `heater-coproc/port/l011/vendor/cmsis_device/{stm32l0xx.h, stm32l011xx.h, system_stm32l0xx.h, system_stm32l0xx.c, startup_stm32l011xx.s, LICENSE.md}`
- Create: `heater-coproc/port/l011/{l011.ld, Makefile, htr_board.h, port.h, port_sys.c, port_gpio.c, main.c, .gitignore}`

**Interfaces:**
- Consumes: `htr_gpio_t`, `htr_line_t` (Plan 1 Task 6), `htr_uart_t`, `htr_nv_t` (Plan 1).
- Produces: `port.h` API (all of it is declared now; `port_uart_*`, `port_i2c_slave_init`, `port_nv` are implemented in Task 2); `const htr_gpio_t port_gpio`; board macros `HB_*`.

- [ ] **Step 1: Vendor the ST device files (pinned)**

```bash
V=heater-coproc/port/l011/vendor/cmsis_device; mkdir -p $V
B=https://raw.githubusercontent.com/STMicroelectronics/cmsis_device_l0/v1.9.4
for f in Include/stm32l0xx.h Include/stm32l011xx.h Include/system_stm32l0xx.h \
         Source/Templates/gcc/startup_stm32l011xx.s Source/Templates/system_stm32l0xx.c LICENSE.md; do
  curl -fsSL "$B/$f" -o "$V/$(basename $f)"
done
ls $V    # 6 files
```

- [ ] **Step 2: Write linker script, board header, port API, system and GPIO layers**

`heater-coproc/port/l011/l011.ld`:

````ld
/* STM32L011F4: 16 KB flash, 2 KB SRAM. */
ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x200;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 2K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
  .text : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
            KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
  .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } > RAM
  ._user_stack : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .); . = . + _Min_Stack_Size; . = ALIGN(8); } > RAM
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
````

`heater-coproc/port/l011/htr_board.h`:

````c
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
````

`heater-coproc/port/l011/port.h`:

````c
#ifndef PORT_H
#define PORT_H
/* EF-L011 port layer: everything MCU-specific behind small functions. */
#include "htr_iface.h"
#include "htr_nv.h"
#include "htr_uart.h"
#include <stdint.h>

void     port_clock_init(void);            /* HSI16 SYSCLK, APB1 /2 = 8 MHz */
void     port_tick_init(void);             /* SysTick 1 ms */
uint32_t port_now(void);
void     port_gpio_init(void);             /* safe levels BEFORE outputs enabled */
extern const htr_gpio_t port_gpio;

void     port_uart_init(void);
void     port_uart_process(uint32_t now);  /* echo-timeout housekeeping */
uint32_t port_uart_echo_errs(void);
extern const htr_uart_t port_uart_3w;
extern const htr_uart_t port_uart_4w;

void     port_i2c_slave_init(uint8_t addr7);
extern const htr_nv_t port_nv;

void     port_iwdg_start(void);
void     port_iwdg_kick(void);
void     port_lock(void);
void     port_unlock(void);
#endif
````

`heater-coproc/port/l011/port_sys.c`:

````c
/* Clock, SysTick, IWDG, critical sections. */
#include "port.h"
#include "stm32l0xx.h"

static volatile uint32_t s_ms;

void port_clock_init(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {}
    /* 16 MHz in the reset voltage range needs 1 flash wait state. */
    FLASH->ACR |= FLASH_ACR_LATENCY;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) == 0u) {}
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_SW_HSI | RCC_CFGR_PPRE1_DIV2;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
    SystemCoreClock = 16000000u;
}

void port_tick_init(void)
{
    (void)SysTick_Config(16000u);
}

void SysTick_Handler(void) { s_ms++; }
uint32_t port_now(void) { return s_ms; }

void port_iwdg_start(void)
{
#ifdef DEBUG
    RCC->APB2ENR |= RCC_APB2ENR_DBGMCUEN;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
#endif
    IWDG->KR = 0xCCCCu;            /* start (LSI ~37 kHz) */
    IWDG->KR = 0x5555u;            /* unlock PR/RLR */
    IWDG->PR = 3u;                 /* /32 -> ~1.16 kHz */
    IWDG->RLR = 2000u;             /* ~1.7 s */
    while (IWDG->SR != 0u) {}
    IWDG->KR = 0xAAAAu;
}

void port_iwdg_kick(void) { IWDG->KR = 0xAAAAu; }
void port_lock(void)   { __disable_irq(); }
void port_unlock(void) { __enable_irq(); }

/* -nostartfiles drops crt0 (whose exit() drags in newlib stdio); the startup
   code still calls __libc_init_array, which needs these hooks. */
void _init(void) {}
void _fini(void) {}
````

`heater-coproc/port/l011/port_gpio.c`:

````c
/* Control lines, line sense and UART pin attach. Levels come from htr_board.h. */
#include "port.h"
#include "htr_board.h"
#include "htr_types.h"

#define MODE_INPUT  0u
#define MODE_OUTPUT 1u
#define MODE_AF     2u
#define MODE_ANALOG 3u

static void pin_mode(GPIO_TypeDef *p, uint32_t pin, uint32_t mode)
{
    p->MODER = (p->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}
static void pin_af(GPIO_TypeDef *p, uint32_t pin, uint32_t af)
{
    volatile uint32_t *r = &p->AFR[pin >> 3];
    uint32_t sh = (pin & 7u) * 4u;
    *r = (*r & ~(0xFu << sh)) | (af << sh);
}
static void pin_write(GPIO_TypeDef *p, uint32_t pin, bool high)
{
    p->BSRR = high ? (1u << pin) : (1u << (pin + 16u));
}
static void line_level(GPIO_TypeDef *p, uint32_t pin, bool active_hi, bool active)
{
    pin_write(p, pin, active ? active_hi : !active_hi);
}

static void g_set_line(void *ctx, htr_line_t l, bool active)
{
    (void)ctx;
    switch (l) {
    case HTR_LINE_TX_MODE: line_level(HB_TX_MODE_PORT, HB_TX_MODE_PIN, HB_TX_MODE_ACTIVE_HI, active); break;
    case HTR_LINE_TX_EN:   line_level(HB_TX_EN_PORT, HB_TX_EN_PIN, HB_TX_EN_ACTIVE_HI, active); break;
    default:               line_level(HB_TX_PWR_PORT, HB_TX_PWR_PIN, HB_TX_PWR_ACTIVE_HI, active); break;
    }
}

static void g_attach(void *ctx, uint8_t wiring, bool attach)
{
    (void)ctx;
    if (wiring == HTR_WIRING_3W) {
        pin_mode(HB_3W_TX_PORT, HB_3W_TX_PIN, attach ? MODE_AF : MODE_INPUT);
        pin_mode(HB_3W_RX_PORT, HB_3W_RX_PIN, attach ? MODE_AF : MODE_INPUT);
    } else if (wiring == HTR_WIRING_4W) {
        pin_mode(HB_4W_TX_PORT, HB_4W_TX_PIN, attach ? MODE_AF : MODE_INPUT);
        pin_mode(HB_4W_RX_PORT, HB_4W_RX_PIN, attach ? MODE_AF : MODE_INPUT);
    }
}

static bool g_rx_level(void *ctx, uint8_t wiring)
{
    (void)ctx;
    if (wiring == HTR_WIRING_4W) return (HB_4W_RX_PORT->IDR & (1u << HB_4W_RX_PIN)) != 0u;
    return (HB_3W_RX_PORT->IDR & (1u << HB_3W_RX_PIN)) != 0u;
}

const htr_gpio_t port_gpio = { g_set_line, g_attach, g_rx_level, 0 };

void port_gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    /* Output latches to the INACTIVE level first, then switch to output, so
       no control line ever glitches active during boot. */
    g_set_line(0, HTR_LINE_TX_EN, false);
    g_set_line(0, HTR_LINE_TX_PWR, false);
    g_set_line(0, HTR_LINE_TX_MODE, false);
    pin_mode(HB_TX_EN_PORT, HB_TX_EN_PIN, MODE_OUTPUT);
    pin_mode(HB_TX_PWR_PORT, HB_TX_PWR_PIN, MODE_OUTPUT);
    pin_mode(HB_TX_MODE_PORT, HB_TX_MODE_PIN, MODE_OUTPUT);
    /* UART pins: AF numbers preset, pins stay inputs until attach. */
    pin_af(HB_3W_TX_PORT, HB_3W_TX_PIN, HB_3W_AF);
    pin_af(HB_3W_RX_PORT, HB_3W_RX_PIN, HB_3W_AF);
    pin_af(HB_4W_TX_PORT, HB_4W_TX_PIN, HB_4W_AF);
    pin_af(HB_4W_RX_PORT, HB_4W_RX_PIN, HB_4W_AF);
    g_attach(0, HTR_WIRING_3W, false);
    g_attach(0, HTR_WIRING_4W, false);
#if HB_GPIOX_DEBUG
    pin_write(HB_GPIOX6_PORT, HB_GPIOX6_PIN, false);
    pin_write(HB_GPIOX7_PORT, HB_GPIOX7_PIN, false);
    pin_mode(HB_GPIOX6_PORT, HB_GPIOX6_PIN, MODE_OUTPUT);
    pin_mode(HB_GPIOX7_PORT, HB_GPIOX7_PIN, MODE_OUTPUT);
#endif
    /* I2C1 pins: AF, open-drain (external 2.2k pull-ups). */
    GPIOA->OTYPER |= (1u << HB_I2C_SCL_PIN) | (1u << HB_I2C_SDA_PIN);
    pin_af(GPIOA, HB_I2C_SCL_PIN, HB_I2C_AF);
    pin_af(GPIOA, HB_I2C_SDA_PIN, HB_I2C_AF);
    pin_mode(GPIOA, HB_I2C_SCL_PIN, MODE_AF);
    pin_mode(GPIOA, HB_I2C_SDA_PIN, MODE_AF);
}
````

Bring-up `main.c` (replaced in Task 2):

`stage1/l011/main.c`:

````c
/* EF-L011 bring-up stage (Plan 2 Task 1): clock, safe GPIO, tick, IWDG.
   Replaced by the full superloop in Task 2. */
#include "port.h"

int main(void)
{
    port_clock_init();
    port_gpio_init();                 /* safe levels before anything else */
    port_tick_init();
    port_iwdg_start();
    for (;;) {
        port_iwdg_kick();
    }
}
````

`heater-coproc/port/l011/.gitignore`:

```
build/
```

- [ ] **Step 3: Write the stage-1 Makefile**

`stage1/l011/Makefile`:

````make
# Makefile -- EF-L011 heater coprocessor (STM32L011F4U3). Invoke as
#   make -C heater-coproc/port/l011
# Produces build/heater-l011.elf/.bin/.hex and fails if flash > 16 KB or
# RAM (incl. 512 B stack) > 2 KB. Needs STM32CubeIDE's arm-none-eabi
# toolchain on PATH (same as boot/Makefile). Flash/debug the .elf with
# CubeIDE ("Debug As > STM32 C/C++ Application" on an imported ELF) or
# STM32_Programmer_CLI.

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SZ      := $(CROSS)size

BUILD  := build
OBJDIR := $(BUILD)/obj
ROOT   := ../../..

ARCH := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DEFS := -DSTM32L011xx $(if $(DEBUG),-DDEBUG,)
INCS := -I. -I$(ROOT)/heater-common -I$(ROOT)/heater-coproc/core \
        -Ivendor/cmsis_device -I$(ROOT)/cube/Drivers/CMSIS/Include

CFLAGS  := $(ARCH) $(DEFS) $(INCS) -std=gnu11 -Os -g3 -Wall -Wextra -Werror \
           -funsigned-char -ffunction-sections -fdata-sections
ASFLAGS := $(ARCH) -x assembler-with-cpp -g3
LDFLAGS := $(ARCH) -T l011.ld --specs=nano.specs --specs=nosys.specs -nostartfiles \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/heater-l011.map \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

SRC_C := main.c port_sys.c port_gpio.c \
         vendor/cmsis_device/system_stm32l0xx.c
SRC_S := vendor/cmsis_device/startup_stm32l011xx.s

OBJS := $(addprefix $(OBJDIR)/,$(notdir $(SRC_C:.c=.o)) $(notdir $(SRC_S:.s=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.s $(sort $(dir $(SRC_S)))

all: $(BUILD)/heater-l011.bin $(BUILD)/heater-l011.hex size-check

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(CC) $(ASFLAGS) -c $< -o $@
$(OBJDIR):
	mkdir -p $@

$(BUILD)/heater-l011.elf: $(OBJS) l011.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SZ) $@
$(BUILD)/heater-l011.bin: $(BUILD)/heater-l011.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/heater-l011.hex: $(BUILD)/heater-l011.elf
	$(OBJCOPY) -O ihex $< $@

# Budget gate (spec: <= 12 KB flash target, hard limit 16 KB).
size-check: $(BUILD)/heater-l011.elf
	@$(SZ) -A $< | awk '/^\.(isr_vector|text|rodata|data|ARM.exidx|preinit_array|init_array|fini_array) /{f+=$$2} /^\.(data|bss|_user_stack) /{r+=$$2} END{printf "flash %d / 16384, ram %d / 2048\n",f,r; if(f>16384||r>2048) exit 1; if(f>12288) print "WARNING: flash above the 12 KB target"}'

clean:
	rm -rf $(BUILD)
.PHONY: all clean size-check
````

- [ ] **Step 4: Build and check size**

Run: `make -C heater-coproc/port/l011 clean all`
Expected: no warnings; last line `flash ~960 / 16384, ram ~520 / 2048`.

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/port/l011
git commit -m "feat(heater-l011): build skeleton, board header, clock/GPIO/IWDG

Vendored ST cmsis_device_l0 v1.9.4 (STM32L011). htr_board.h holds every
schematic-derived value (R0.00.00 reading, to be confirmed at bench M2).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 2: UARTs, I²C slave, EEPROM settings, full superloop

**Files:**
- Create: `heater-coproc/port/l011/port_uart.c`, `port_i2c_slave.c`, `port_nv.c`
- Modify: `heater-coproc/port/l011/main.c` (replace), `heater-coproc/port/l011/Makefile` (replace)

**Interfaces:**
- Consumes: `htr_regs_*`, `htr_fsm_*`, `drv_vevor`, `drv_autoterm`, `HTR_I2C_ADDR7`, `HTR_TYPE_*` (Plan 1).
- Produces: `port_uart_3w`, `port_uart_4w` (`htr_uart_t`), `port_uart_init/process/echo_errs`, `port_i2c_slave_init(uint8_t addr7)`, `port_nv` (`htr_nv_t`), final `main()`.

- [ ] **Step 1: Write the UART layer**

`heater-coproc/port/l011/port_uart.c`:

````c
/* IRQ-driven byte rings for LPUART1 (3-wire) and USART2 (4-wire).
   3-wire is half-duplex on one wire: every byte we transmit comes back on
   RX; it is matched against an echo queue and dropped (mismatch counts as
   an echo error = TX-path diagnostic). */
#include "port.h"
#include "htr_board.h"
#include <stdbool.h>

#define RX_SZ   64u
#define TX_SZ   32u
#define ECHO_SZ 32u
#define ECHO_TIMEOUT_MS 20u
#define PCLK1_HZ 8000000u

typedef struct {
    USART_TypeDef *u;
    bool lp;                   /* LPUART: BRR = 256*f/baud */
    bool half;                 /* strip local echo */
    volatile uint8_t rx[RX_SZ];
    volatile uint8_t rx_h, rx_t;
    volatile uint8_t tx[TX_SZ];
    volatile uint8_t tx_h, tx_t;
    volatile uint8_t echo[ECHO_SZ];
    volatile uint8_t e_h, e_t;
    volatile uint32_t last_tx_ms;
} pu_t;

static pu_t s_3w;   /* zero-init (.bss); identity set in port_uart_init() */
static pu_t s_4w;
static volatile uint32_t s_echo_errs;

static uint32_t brr(const pu_t *p, uint32_t baud)
{
    return p->lp ? (uint32_t)(((uint64_t)PCLK1_HZ * 256u + baud / 2u) / baud)
                 : (PCLK1_HZ + baud / 2u) / baud;
}

static void cfg(pu_t *p, uint32_t baud, bool txinv, bool rxinv)
{
    p->u->CR1 = 0u;
    p->u->CR2 = (txinv ? USART_CR2_TXINV : 0u) | (rxinv ? USART_CR2_RXINV : 0u);
    p->u->BRR = brr(p, baud);
    p->u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
}

static void irq(pu_t *p)
{
    uint32_t isr = p->u->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        p->u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
    }
    if (isr & USART_ISR_RXNE) {
        uint8_t b = (uint8_t)p->u->RDR;
        if (p->half && (p->e_t != p->e_h)) {
            uint8_t exp = p->echo[p->e_t];
            p->e_t = (uint8_t)((p->e_t + 1u) % ECHO_SZ);
            if (b != exp) s_echo_errs++;
        } else {
            uint8_t n = (uint8_t)((p->rx_h + 1u) % RX_SZ);
            if (n != p->rx_t) { p->rx[p->rx_h] = b; p->rx_h = n; }
        }
    }
    if ((p->u->CR1 & USART_CR1_TXEIE) && (isr & USART_ISR_TXE)) {
        if (p->tx_t != p->tx_h) {
            uint8_t b = p->tx[p->tx_t];
            p->tx_t = (uint8_t)((p->tx_t + 1u) % TX_SZ);
            if (p->half) {
                p->echo[p->e_h] = b;
                p->e_h = (uint8_t)((p->e_h + 1u) % ECHO_SZ);
            }
            p->last_tx_ms = port_now();
            p->u->TDR = b;
        } else {
            p->u->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

void LPUART1_IRQHandler(void) { irq(&s_3w); }
void USART2_IRQHandler(void)  { irq(&s_4w); }

static size_t u_write(void *ctx, const uint8_t *buf, size_t len)
{
    pu_t *p = (pu_t *)ctx;
    uint8_t used = (uint8_t)((p->tx_h + TX_SZ - p->tx_t) % TX_SZ);
    size_t i;
    if (len > (size_t)(TX_SZ - 1u - used)) return 0u;      /* all or nothing */
    for (i = 0u; i < len; ++i) {
        p->tx[p->tx_h] = buf[i];
        p->tx_h = (uint8_t)((p->tx_h + 1u) % TX_SZ);
    }
    p->u->CR1 |= USART_CR1_TXEIE;
    return len;
}

static size_t u_read(void *ctx, uint8_t *buf, size_t cap)
{
    pu_t *p = (pu_t *)ctx;
    size_t n = 0u;
    while ((n < cap) && (p->rx_t != p->rx_h)) {
        buf[n++] = p->rx[p->rx_t];
        p->rx_t = (uint8_t)((p->rx_t + 1u) % RX_SZ);
    }
    return n;
}

static void u_flush(void *ctx)
{
    pu_t *p = (pu_t *)ctx;
    p->rx_t = p->rx_h;
}

static bool u_set_baud(void *ctx, uint32_t baud)
{
    pu_t *p = (pu_t *)ctx;
    if ((p->tx_t != p->tx_h) || ((p->u->ISR & USART_ISR_TC) == 0u)) return false;
    p->u->CR1 &= ~USART_CR1_UE;
    p->u->BRR = brr(p, baud);
    p->u->CR1 |= USART_CR1_UE;
    return true;
}

const htr_uart_t port_uart_3w = { u_write, u_read, u_flush, u_set_baud, &s_3w };
const htr_uart_t port_uart_4w = { u_write, u_read, u_flush, u_set_baud, &s_4w };

void port_uart_init(void)
{
    s_3w.u = HB_3W_UART; s_3w.lp = true;  s_3w.half = true;
    s_4w.u = HB_4W_UART; s_4w.lp = false; s_4w.half = false;
    RCC->APB1ENR |= RCC_APB1ENR_LPUART1EN | RCC_APB1ENR_USART2EN;  /* both on PCLK1 */
    cfg(&s_3w, 4800u, HB_3W_TX_INVERT, HB_3W_RX_INVERT);
    cfg(&s_4w, 2400u, HB_4W_TX_INVERT, HB_4W_RX_INVERT);
    NVIC_SetPriority(HB_3W_IRQn, 1u);
    NVIC_SetPriority(HB_4W_IRQn, 1u);
    NVIC_EnableIRQ(HB_3W_IRQn);
    NVIC_EnableIRQ(HB_4W_IRQn);
}

/* An echo that never comes back means the RX path or the wire is broken. */
void port_uart_process(uint32_t now)
{
    port_lock();
    if ((s_3w.e_t != s_3w.e_h) && (s_3w.tx_t == s_3w.tx_h) &&
        ((uint32_t)(now - s_3w.last_tx_ms) > ECHO_TIMEOUT_MS)) {
        s_3w.e_t = s_3w.e_h;
        s_echo_errs++;
    }
    port_unlock();
}

uint32_t port_uart_echo_errs(void) { return s_echo_errs; }
````

- [ ] **Step 2: Write the I²C slave and EEPROM layers**

`heater-coproc/port/l011/port_i2c_slave.c`:

````c
/* I2C1 slave at the G0B1-facing address. Byte-level events go straight to
   the portable register file (htr_regs_i2c_*). Clock stretching enabled. */
#include "port.h"
#include "htr_board.h"
#include "htr_regs.h"

void port_i2c_slave_init(uint8_t addr7)
{
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_I2C1SEL) | RCC_CCIPR_I2C1SEL_1;   /* HSI16 */
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1->CR1 = 0u;
    I2C1->TIMINGR = HB_I2C_TIMINGR;
    I2C1->OAR1 = 0u;
    I2C1->OAR1 = I2C_OAR1_OA1EN | ((uint32_t)addr7 << 1);
    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE |
                I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
    NVIC_SetPriority(I2C1_IRQn, 0u);
    NVIC_EnableIRQ(I2C1_IRQn);
}

void I2C1_IRQHandler(void)
{
    uint32_t isr = I2C1->ISR;
    if (isr & I2C_ISR_ADDR) {
        bool rd = (isr & I2C_ISR_DIR) != 0u;
        if (rd) I2C1->ISR |= I2C_ISR_TXE;          /* drop any stale preloaded byte */
        htr_regs_i2c_begin(rd);
        I2C1->ICR = I2C_ICR_ADDRCF;
    }
    if (isr & I2C_ISR_RXNE) htr_regs_i2c_rx((uint8_t)I2C1->RXDR);
    if (isr & I2C_ISR_TXIS) I2C1->TXDR = htr_regs_i2c_tx();
    if (isr & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF;
    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        htr_regs_i2c_end();
    }
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    }
}
````

`heater-coproc/port/l011/port_nv.c`:

````c
/* Settings in the L011 data EEPROM: one word = magic, type, timeout, check. */
#include "port.h"
#include "stm32l0xx.h"

#define NV_ADDR  (DATA_EEPROM_BASE)
#define NV_MAGIC 0xA5u

static bool nv_load(void *ctx, uint8_t *type, uint8_t *timeout)
{
    uint32_t w = *(volatile const uint32_t *)NV_ADDR;
    uint8_t m = (uint8_t)w, t = (uint8_t)(w >> 8), to = (uint8_t)(w >> 16), c = (uint8_t)(w >> 24);
    (void)ctx;
    if ((m != NV_MAGIC) || (c != (uint8_t)(m ^ t ^ to ^ 0xFFu))) return false;
    *type = t;
    *timeout = to;
    return true;
}

static void nv_save(void *ctx, uint8_t type, uint8_t timeout)
{
    uint32_t w = NV_MAGIC | ((uint32_t)type << 8) | ((uint32_t)timeout << 16) |
                 ((uint32_t)(uint8_t)(NV_MAGIC ^ type ^ timeout ^ 0xFFu) << 24);
    (void)ctx;
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = 0x89ABCDEFu;
        FLASH->PEKEYR = 0x02030405u;
    }
    *(volatile uint32_t *)NV_ADDR = w;          /* auto-erase + program, ~3.2 ms */
    while (FLASH->SR & FLASH_SR_BSY) {}
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

const htr_nv_t port_nv = { nv_load, nv_save, 0 };
````

- [ ] **Step 3: Replace `main.c` and the Makefile with the full versions**

`heater-coproc/port/l011/main.c`:

````c
/* EF-L011 heater coprocessor: init, then a non-blocking superloop. */
#include "port.h"
#include "htr_board.h"
#include "htr_fsm.h"
#include "htr_i2c_map.h"
#include "htr_regs.h"
#include "drv_autoterm.h"
#include "drv_vevor.h"

static const htr_driver_t *const s_drivers[] = { &drv_vevor, &drv_autoterm };

int main(void)
{
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
        htr_regs_publish(now, port_uart_echo_errs());
        port_iwdg_kick();
    }
}
````

`heater-coproc/port/l011/Makefile`:

````make
# Makefile -- EF-L011 heater coprocessor (STM32L011F4U3). Invoke as
#   make -C heater-coproc/port/l011
# Produces build/heater-l011.elf/.bin/.hex and fails if flash > 16 KB or
# RAM (incl. 512 B stack) > 2 KB. Needs STM32CubeIDE's arm-none-eabi
# toolchain on PATH (same as boot/Makefile). Flash/debug the .elf with
# CubeIDE ("Debug As > STM32 C/C++ Application" on an imported ELF) or
# STM32_Programmer_CLI.

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SZ      := $(CROSS)size

BUILD  := build
OBJDIR := $(BUILD)/obj
ROOT   := ../../..

ARCH := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DEFS := -DSTM32L011xx $(if $(DEBUG),-DDEBUG,)
INCS := -I. -I$(ROOT)/heater-common -I$(ROOT)/heater-coproc/core \
        -Ivendor/cmsis_device -I$(ROOT)/cube/Drivers/CMSIS/Include

CFLAGS  := $(ARCH) $(DEFS) $(INCS) -std=gnu11 -Os -g3 -Wall -Wextra -Werror \
           -funsigned-char -ffunction-sections -fdata-sections
ASFLAGS := $(ARCH) -x assembler-with-cpp -g3
LDFLAGS := $(ARCH) -T l011.ld --specs=nano.specs --specs=nosys.specs -nostartfiles \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/heater-l011.map \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

SRC_C := main.c port_sys.c port_gpio.c port_uart.c port_i2c_slave.c port_nv.c \
         vendor/cmsis_device/system_stm32l0xx.c \
         $(ROOT)/heater-common/crc16_modbus.c \
         $(ROOT)/heater-common/vevor_proto.c \
         $(ROOT)/heater-common/autoterm_proto.c \
         $(ROOT)/heater-coproc/core/drv_vevor.c \
         $(ROOT)/heater-coproc/core/drv_autoterm.c \
         $(ROOT)/heater-coproc/core/htr_iface.c \
         $(ROOT)/heater-coproc/core/htr_fsm.c \
         $(ROOT)/heater-coproc/core/htr_regs.c
SRC_S := vendor/cmsis_device/startup_stm32l011xx.s

OBJS := $(addprefix $(OBJDIR)/,$(notdir $(SRC_C:.c=.o)) $(notdir $(SRC_S:.s=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.s $(sort $(dir $(SRC_S)))

all: $(BUILD)/heater-l011.bin $(BUILD)/heater-l011.hex size-check

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(CC) $(ASFLAGS) -c $< -o $@
$(OBJDIR):
	mkdir -p $@

$(BUILD)/heater-l011.elf: $(OBJS) l011.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SZ) $@
$(BUILD)/heater-l011.bin: $(BUILD)/heater-l011.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/heater-l011.hex: $(BUILD)/heater-l011.elf
	$(OBJCOPY) -O ihex $< $@

# Budget gate (spec: <= 12 KB flash target, hard limit 16 KB).
size-check: $(BUILD)/heater-l011.elf
	@$(SZ) -A $< | awk '/^\.(isr_vector|text|rodata|data|ARM.exidx|preinit_array|init_array|fini_array) /{f+=$$2} /^\.(data|bss|_user_stack) /{r+=$$2} END{printf "flash %d / 16384, ram %d / 2048\n",f,r; if(f>16384||r>2048) exit 1; if(f>12288) print "WARNING: flash above the 12 KB target"}'

clean:
	rm -rf $(BUILD)
.PHONY: all clean size-check
````

- [ ] **Step 4: Build, check size, re-run host suite**

Run: `make -C heater-coproc/port/l011 clean all`
Expected: no warnings; `flash ~10372 / 16384, ram ~1456 / 2048` (no "above the 12 KB target" warning).
Run: `cmake -S Tests -B Tests/build >/dev/null && cmake --build Tests/build && (cd Tests/build && ctest)`
Expected: `100% tests passed` (port code is not host-built; this guards against accidental core edits).

- [ ] **Step 5: Commit**

```bash
git add heater-coproc/port/l011
git commit -m "feat(heater-l011): UART rings + 3-wire echo strip, I2C1 slave, EEPROM, superloop

Full image ~10.4 KB flash / ~1.5 KB RAM on the STM32L011F4.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```

---

### Task 3: Bring-up guide

**Files:**
- Create: `docs/heater-coprocessor.md`

- [ ] **Step 1: Write the guide**

`docs/heater-coprocessor.md`:

````markdown
# Heater coprocessor (EF-L011) — build, flash, bench bring-up

Design: `docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md`.
I²C contract: `heater-common/htr_i2c_map.h` (slave 0x64, map version 1).

## Layout

| Path | What |
|---|---|
| `heater-common/` | shared portable code (I²C map, types, VEVOR + AUTOTERM protocols) |
| `heater-coproc/core/` | portable coprocessor core (drivers, FSM + auto-detect, iface, register file) |
| `heater-coproc/port/l011/` | EF-L011 port: `htr_board.h` (the only schematic-derived file), port_*.c, `main.c`, `Makefile`, `l011.ld`, vendored ST CMSIS device files |
| `App/services/heater.c`, `mbp_heater.c` | G0B1 client + Modbus regs 53–74 |

## Build

```bash
export PATH="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin:$PATH"
make -C heater-coproc/port/l011            # release
make -C heater-coproc/port/l011 DEBUG=1    # freezes IWDG while halted
```

Output: `heater-coproc/port/l011/build/heater-l011.{elf,bin,hex}` and a size line
`flash N / 16384, ram N / 2048` (fails the build above the hard limits, warns above 12 KB).

## Flash / debug (ST-LINK on HDR1)

- CLI: `STM32_Programmer_CLI -c port=SWD mode=UR -w heater-coproc/port/l011/build/heater-l011.hex -v -rst`
- CubeIDE: File ▸ Import ▸ C/C++ ▸ "STM32 Cortex-M Executable", pick `build/heater-l011.elf`, MCU `STM32L011F4Ux`, then Debug.
  Build with `DEBUG=1` so the IWDG stops while halted.

## Before the first power-up: confirm `htr_board.h` against the finished schematic

Every value in `heater-coproc/port/l011/htr_board.h` was read from the R0.00.00 PDF.
Tick each against the **finished** schematic (and the STM32L011x4 datasheet for AF numbers) and fix the header before M2:

- [ ] 3-wire side = LPUART1 (PA0 RX / PA1 TX, AF6); TX inverted, RX not
- [ ] 4-wire side = USART2 (PB6 TX / PB7 RX, AF0); no inversion
- [ ] HTR_TX_MODE (PA6): level that selects the **4-wire** path
- [ ] HTR_TX_nEN (PA7): level that **connects** the TX path (expected low)
- [ ] HTR_TX_PWR / HTR_5V_EN (PB1): level that turns the TX supply **on** (expected high)
- [ ] `HB_PWR_FOR_3W`: does the 3-wire path need +5V_HTR? (expected no)
- [ ] GPIOX6 / GPIOX7 (PA4/PA5): role; leave `HB_GPIOX_DEBUG 0` unless unloaded test points
- [ ] I²C1 PA9 SCL / PA10 SDA, AF1

## Bench milestones (each gates the next)

**M1 — boot.** Flash; in the debugger confirm `SystemCoreClock == 16000000` and that `port_now()` advances.
Let it run 60 s un-halted (release build): no IWDG reset loop (no NRST pulses on a scope).

**M2 — safe GPIO.** Scope PA6, PA7, PB1 from power-on: each must sit at its *inactive* level the whole time
(mux disconnected, +5V_HTR off), with no glitch during reset. With nothing on HDR3 the FSM stays in
DETECTING and never enables the iface — PB1 stays off. Then force a type to watch the enable sequence:
write `0x02` to reg 0x10 (see M3) and confirm order `nEN off → MODE → PWR on → (5 ms) → nEN on`.

**M3 — I²C.** From the G0B1 (Plan 3 build) watch Modbus regs 68–74 / `heater_get_telemetry()`:
`present == 1`. Or from a G0B1 GDB session: `HAL_I2C_Mem_Read(&hi2c2, 0x64<<1, 0x00, 1, buf, 2, 10)` → `EF 01`.
Confirm the IS31FL3293 still ACKs at 0x6C on the same bus.

**M4 — UARTs, no heater.** Loop an FT232 onto each side in turn:
- 3-wire: force type 1 (VEVOR); the FT232 (RX non-inverted) must see a 16-byte STOP poll (`AA 66 06 0B … 05 …`) once a second at 4800 8N1.
  `ECHO_ERRS` (reg 0x3E) must stay 0 — every byte we send comes back through the comparator and is stripped. A climbing `ECHO_ERRS` = TX path / inversion wrong.
- 4-wire: force type 2 (AUTOTERM); the FT232 must see `AA 03 00 00 1C 95 3D` ×35 at 2400, then `… 06 5E BC` ×5 at 4800, then `… 06 5E BC` at 9600.

**M5 — line sense.** Set reg 0x10 = 0xFF (AUTO), command 5 (AUTO_DETECT) with: nothing connected, the VEVOR, the AUTOTERM.
Record reg 0x44 LINE_SENSE each time. Expected: none → class 0; VEVOR → bit0 + class 1; AUTOTERM → bit1 + class 2.
**Record the AUTOTERM result while the heater is asleep** (spec open item 4) — if its TX does not idle high, auto-detect cannot see it and it must be forced.

**M6 — VEVOR live.** Auto-detect → `ACTIVE_TYPE 1`, `SAFE_OFF` set. Fuelled: RUN_REQUEST 1, level 1 → STARTING → RUNNING; step levels; RUN_REQUEST 0 → STOP_REQUESTED → COOLDOWN → OFF + SAFE_OFF. Do not cut 12 V until SAFE_OFF.

**M7 — AUTOTERM live.** Auto-detect → `ACTIVE_TYPE 2`. Start at a setpoint; **write down the steady full-power VENDOR_STATE/VENDOR_SUB** (spec open item 5; currently assumed 2.4+/3.x = RUNNING). Stop → cooldown (state 4.x) → 0.1 standby. Note whether the heater needs `0x04`/`0x0B` (it should not; the driver never sends them).

**M8 — full chain.** Cortex HeaterCard → Modbus → G0B1 → coprocessor → heater start/stop. Then, while running, disconnect the G0B1 I²C (or halt the G0B1): after `HOST_TIMEOUT_MIN` (set it to 1 for the test) the coprocessor must run a normal stop + cooldown and set `HOST_TIMEOUT_STOP` (FLAGS bit 5).

## Register cheat sheet (full map: `heater-common/htr_i2c_map.h`)

| Reg | Name | Notes |
|---|---|---|
| 0x00/0x01 | DEVICE_ID / MAP_VERSION | `EF 01` |
| 0x10 | HEATER_TYPE | FF auto, 0 none, 1 VEVOR, 2 AUTOTERM (persisted) |
| 0x11–0x15 | RUN_REQUEST, LEVEL, SETPOINT_C, AMBIENT_C_X10 | G0B1 rewrites every 250 ms |
| 0x16 | HOST_TIMEOUT_MIN | default 10, 0 = off (bench only) |
| 0x18 / 0x19 | COMMAND / CMD_RESULT | 1 start 2 stop 3 clear-fault 4 reinit 5 auto-detect |
| 0x20 | PHASE | 0 off 1 detecting 2 starting 3 running 4 stop-req 5 cooldown 6 fault |
| 0x22 | FLAGS | b0 present b1 fresh b2 safe-off b3 comms-fault b4 iface b5 host-timeout b6 echo-fault b7 detect-done b8 fault-latched |
| 0x26/0x27 | VENDOR_STATE / SUB | raw (AUTOTERM major.minor) |
| 0x3E | ECHO_ERRS | 3-wire TX-path health |
| 0x44 / 0x45 | LINE_SENSE / DETECT_RESULT | auto-detect diagnostics |
````

- [ ] **Step 2: Check every path and command in the guide exists**

Run: `for p in heater-common/htr_i2c_map.h heater-coproc/port/l011/htr_board.h heater-coproc/port/l011/Makefile docs/superpowers/specs/2026-10-06-heater-coprocessor-design.md; do test -e $p && echo ok $p || echo MISSING $p; done`
Expected: four `ok` lines.

- [ ] **Step 3: Commit**

```bash
git add docs/heater-coprocessor.md
git commit -m "docs: heater coprocessor build/flash/bench bring-up guide

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017uxgfN1Y67gVKWmTWmpxxk"
```
