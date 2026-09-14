#ifndef MBP_BOOT_H
#define MBP_BOOT_H

#define MBP_BOOT_ARM_VALUE 0x00A5u

/* Registers reg 35 (enter-bootloader command). A write of MBP_BOOT_ARM_VALUE
   refuses (MB_EXC_SLAVE_DEVICE_FAILURE) while engine_running() is non-zero;
   otherwise it invokes enter_bl() (expected to arm the boot magic and reset).
   Any other written value is a no-op (MB_EXC_NONE). Reg-35 read returns 0. */
void mbp_boot_register(int (*engine_running)(void), void (*enter_bl)(void));

#endif /* MBP_BOOT_H */
