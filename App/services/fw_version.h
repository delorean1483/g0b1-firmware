#ifndef FW_VERSION_H
#define FW_VERSION_H

/* gobi APU firmware version -- reg 2 (read-only).
 * v1.0.0: first release with auto-fan + Component Test.
 * v1.1.0: first A/B remote-update-capable app (STM32 bootloader + slot
 * self-confirm; see docs/remote-update.md).
 * v1.1.1: bench OTA round-trip validation build (proves a real version
 * change streams over RS-485 into the inactive slot). */
#define G0B1_FW_VERSION_MAJOR 1u
#define G0B1_FW_VERSION_MINOR 1u
#define G0B1_FW_VERSION_PATCH 1u

/* single-register encoding: major*10000 + minor*100 + patch  (max 6.55.35) */
#define G0B1_FW_VERSION_ENC \
    (G0B1_FW_VERSION_MAJOR*10000u + G0B1_FW_VERSION_MINOR*100u + G0B1_FW_VERSION_PATCH)

#endif /* FW_VERSION_H */
