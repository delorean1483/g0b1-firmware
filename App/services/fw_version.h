#ifndef FW_VERSION_H
#define FW_VERSION_H

/* gobi APU firmware version -- reg 2 (read-only).
 * v1.0.0: first release with auto-fan + Component Test.
 * v1.1.0: first A/B remote-update-capable app (STM32 bootloader + slot
 * self-confirm; see docs/remote-update.md).
 * v1.1.1: bench OTA round-trip validation build (proves a real version
 * change streams over RS-485 into the inactive slot).
 * v1.1.2: first remote-OTA go-live build -- carries the reset-magic/_estack
 * linker fix (enter-bootloader now reliable, PR #5) and the RPM tach-filter
 * fix (ICFilter + recalibrated constant, PR #3). First STM32 firmware shipped
 * via the dashboard APU-flash path.
 * v1.1.3: version-only bump (no functional change) to exercise the remote
 * APU-flash path end-to-end after the agent's INFO/VERIFY handshake-retry
 * hardening (cortex PR #55/#56) -- gives the flow something newer than the
 * installed v1.1.2 to install.
 * v1.1.4: version-only bump (no functional change) to HW-validate the cortex
 * RS-485 reply-framing fix (idle-gap 5->30ms, cortex PR #60) -- the STM32 is
 * already on v1.1.3, so this gives the flash flow a newer target to install.
 * v1.1.5: machine/engine/oil hours are committed to NVM as they tick -- they
 * previously lived only in the RAM shadow and rolled back on any STM32 reset.
 * v1.1.6: heater coprocessor client (I2C 0x64) replaces the WK2132/VEVOR path;
 * Modbus heater regs extended to 53-75.
 * v1.1.7: external RGB light (IS31FL3293 @0x6C on I2C2) + Modbus regs 76-79. */
#define G0B1_FW_VERSION_MAJOR 1u
#define G0B1_FW_VERSION_MINOR 1u
#define G0B1_FW_VERSION_PATCH 7u

/* single-register encoding: major*10000 + minor*100 + patch  (max 6.55.35) */
#define G0B1_FW_VERSION_ENC \
    (G0B1_FW_VERSION_MAJOR*10000u + G0B1_FW_VERSION_MINOR*100u + G0B1_FW_VERSION_PATCH)

#endif /* FW_VERSION_H */
