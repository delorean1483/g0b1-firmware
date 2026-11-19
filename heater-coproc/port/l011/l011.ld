/* STM32L011F4: 16 KB flash, 2 KB SRAM. */
ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x200;
/* Reserve the first 4 KiB for the resident loader and the last 128-byte
   flash page for its application manifest in the relocated build. */
_app_offset = DEFINED(l011_bootloader_layout) ? 0x1000 : 0;
_manifest_size = DEFINED(l011_bootloader_layout) ? 0x80 : 0;
_loader_size = DEFINED(l011_resident_loader) ? 0x1000 : 16K;
_retained_size = (DEFINED(l011_bootloader_layout) || DEFINED(l011_resident_loader)) ? 8 : 0;

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000 + _app_offset, LENGTH = _loader_size - _app_offset - _manifest_size
  RAM   (xrw) : ORIGIN = 0x20000000 + _retained_size, LENGTH = 2K - _retained_size
}

SECTIONS
{
  .isr_vector : { . = ALIGN(4); __app_vectors = .; KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
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
