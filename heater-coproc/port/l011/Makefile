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
