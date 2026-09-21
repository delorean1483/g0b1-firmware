/* g0b1-boot.ld — STM32G0B1RET3 bootloader linker script.
 *
 * FLASH shrunk to the 32 KB boot region at 0x08000000. RAM has a 32-byte
 * .noinit region carved from the very top (0x20023FE0) that the reset-magic
 * word (g_boot_magic) lives in and that the startup code must NOT zero.
 *
 * R12: the boot flash driver (drv_iflash) is standard flash-resident HAL —
 * there is NO .RamFunc section here and no RAM-copy loop in the startup.
 *
 * _estack MUST sit a guard gap below the NOINIT magic (0x20023FE0). Previously
 * _estack == the magic address, so the bootloader's OWN stack overwrote
 * g_boot_magic during HAL_Init/clock/IWDG before boot_main read it — silently
 * dropping the app's enter-update request (confirmed on silicon 2026-09-21).
 * _estack + RAM end now sit 32 B below NOINIT; magic stays pinned at 0x20023FE0
 * so the app slot linkers and this one still agree on its address.
 */
ENTRY(Reset_Handler)
_estack = 0x20023FC0;              /* 32 B guard below the .noinit magic (0x20023FE0) */
_Min_Heap_Size = 0x0;
_Min_Stack_Size = 0x400;

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 143K + 960   /* 0x23FC0: 32 B guard below NOINIT */
  NOINIT (rw)  : ORIGIN = 0x20023FE0, LENGTH = 32
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH
  .text : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
            KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } >FLASH
  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } >RAM AT> FLASH
  .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } >RAM
  .noinit (NOLOAD) : { . = ALIGN(4); *(.noinit) *(.noinit*) . = ALIGN(4); } >NOINIT
  ._user_heap_stack : { . = ALIGN(8); . = . + _Min_Stack_Size; . = ALIGN(8); } >RAM
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
