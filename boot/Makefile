# Makefile — g0b1 bootloader (STM32G0B1RET3). Invoke as `make -C boot`.
# Produces build/g0b1-boot.elf + build/g0b1-boot.bin, linked at 0x08000000,
# and asserts the flash footprint (text+data) fits the 32 KB boot region.
#
# The arm-none-eabi toolchain (STM32CubeIDE's gnu-tools-for-stm32 13.3) must be
# on PATH.

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SZ      := $(CROSS)size

BUILD  := build
OBJDIR := $(BUILD)/obj

# ---- architecture / defines / includes ------------------------------------
ARCH := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DEFS := -DSTM32G0B1xx -DUSE_HAL_DRIVER

HALDIR := ../cube/Drivers/STM32G0xx_HAL_Driver/Src

INCS := \
  -Isrc \
  -I../App/services \
  -I../App/include \
  -I../cube/Core/Inc \
  -I../cube/Drivers/STM32G0xx_HAL_Driver/Inc \
  -I../cube/Drivers/CMSIS/Device/ST/STM32G0xx/Include \
  -I../cube/Drivers/CMSIS/Include

CFLAGS  := $(ARCH) $(DEFS) $(INCS) -std=gnu11 -Os -g3 \
           -ffunction-sections -fdata-sections
ASFLAGS := $(ARCH) -x assembler-with-cpp -g3

LDFLAGS := $(ARCH) -T g0b1-boot.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/g0b1-boot.map \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments

# ---- sources ---------------------------------------------------------------
BOOT_S := src/startup_g0b1_boot.s
BOOT_C := \
  src/system_boot.c \
  src/boot_it.c \
  src/boot_clock.c \
  src/boot_uart.c \
  src/boot_main.c \
  src/boot_jump.c \
  src/bl_update_mode.c

# portable services + the concrete internal-flash backend (linked, not modified)
PORT := \
  ../App/services/crc32.c \
  ../App/services/bootcfg.c \
  ../App/services/boot_decision.c \
  ../App/services/bl_proto.c \
  ../App/services/bl_session.c \
  ../App/services/modbus_crc.c \
  ../App/services/modbus_frame.c
DRV := ../cube/Core/Src/drv_iflash.c

# minimal HAL set that links (dma.c is required because the UART handle type
# embeds DMA_HandleTypeDef* even though the boot UART never starts a DMA xfer)
HAL := \
  $(HALDIR)/stm32g0xx_hal.c \
  $(HALDIR)/stm32g0xx_hal_rcc.c \
  $(HALDIR)/stm32g0xx_hal_rcc_ex.c \
  $(HALDIR)/stm32g0xx_hal_gpio.c \
  $(HALDIR)/stm32g0xx_hal_dma.c \
  $(HALDIR)/stm32g0xx_hal_flash.c \
  $(HALDIR)/stm32g0xx_hal_flash_ex.c \
  $(HALDIR)/stm32g0xx_hal_uart.c \
  $(HALDIR)/stm32g0xx_hal_uart_ex.c \
  $(HALDIR)/stm32g0xx_hal_cortex.c \
  $(HALDIR)/stm32g0xx_hal_iwdg.c \
  $(HALDIR)/stm32g0xx_hal_pwr.c \
  $(HALDIR)/stm32g0xx_hal_pwr_ex.c

VENDOR := $(PORT) $(DRV) $(HAL)

VPATH := src:../App/services:../cube/Core/Src:$(HALDIR)

S_OBJ      := $(addprefix $(OBJDIR)/,$(notdir $(BOOT_S:.s=.o)))
BOOT_OBJ   := $(addprefix $(OBJDIR)/,$(notdir $(BOOT_C:.c=.o)))
VENDOR_OBJ := $(addprefix $(OBJDIR)/,$(notdir $(VENDOR:.c=.o)))
ALL_OBJ    := $(S_OBJ) $(BOOT_OBJ) $(VENDOR_OBJ)

# our code is held to -Werror; the vendor/portable tree is warned but not fatal
$(BOOT_OBJ):   EXTRAW := -Wall -Wextra -Werror
$(VENDOR_OBJ): EXTRAW := -Wall

# ---- rules -----------------------------------------------------------------
.PHONY: all clean
all: $(BUILD)/g0b1-boot.bin sizecheck

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(EXTRAW) -c $< -o $@

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/g0b1-boot.elf: $(ALL_OBJ) g0b1-boot.ld
	$(CC) $(LDFLAGS) $(ALL_OBJ) -o $@
	$(SZ) $@

$(BUILD)/g0b1-boot.bin: $(BUILD)/g0b1-boot.elf
	$(OBJCOPY) -O binary $< $@

.PHONY: sizecheck
sizecheck: $(BUILD)/g0b1-boot.elf
	@tot=$$($(SZ) $< | awk 'NR==2{print $$1+$$2}'); \
	echo "flash footprint (text+data) = $$tot bytes  (limit 32768)"; \
	if [ $$tot -gt 32768 ]; then echo "ERROR: bootloader exceeds 32 KB"; exit 1; fi

clean:
	rm -rf $(BUILD)
