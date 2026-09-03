#ifndef G0B1_SLOTS_H
#define G0B1_SLOTS_H
/* Single source of truth for flash geometry (keep in sync with all linker scripts). */
#define BOOT_BASE     0x08000000u
#define BOOT_SIZE     0x8000u      /* 32 KB */
#define SLOTA_BASE    0x08008000u
#define SLOTB_BASE    0x08040000u
#define APP_SLOT_SIZE 0x38000u     /* 224 KB */
#define BOOTCFG_BASE  0x08078000u
#define NOINIT_ADDR   0x20023FE0u  /* top 32 B of 144 KB RAM; reset-magic lives here */
#endif
