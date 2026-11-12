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
 * v1.1.7: external RGB light (IS31FL3293 @0x6C on I2C2) + Modbus regs 76-79.
 * v1.1.90: bench test only: I2C2 50 kHz, heater/RGB timeouts 20 ms.
 * v1.1.91: shared I2C2 bus recovery with bounded retry and diagnostics.
 * v1.1.92: full lamp configuration per apply and 1 s idle refresh.
 * v1.1.93: 1 s shared recovery cooldown, no automatic heater COMMAND replay;
 *          matched 50 kHz / 20 ms bench profile.
 * v1.1.94: same changes at 100 kHz / 10 ms for the comparison trial.
 * v1.1.95: bench build (branch bench/2026-11-05-nonreset-restore, cda0c39):
 *          PWM-only applies, non-resetting restore, startup-only reset,
 *          held-line-only cooldown, 100 kHz / 10 ms.
 * v1.1.96: release candidate: shared I2C2 recovery (1 s cooldown that only
 *          skips the HAL while a line is held or a clear failed, no heater
 *          COMMAND replay) at 100 kHz / 20 ms; lamp back to PWM-only applies
 *          and a 10 s refresh, failed transfers restored without the 0x3F
 *          reset (reset re-init only after 3 consecutive failures).
 * v1.1.97: bench build with three consecutive I2C2 failures before recovery
 *          and an F1-style GPIO sequence when both lines are released.
 * v1.1.98: keep the IS31 in software shutdown while requested RGB is zero.
 * v1.1.99: explicitly disable each IS31 output whose PWM is zero.
 * v1.2.1: restore PWM mode on applies and verify mode/control readback.
 *          1.2.0 is reserved for the separate unflashed timing candidate.
 * v1.2.2: explicitly address each mode/control register read.
 * v1.2.3: one-second refresh/retry cap; fresh commands bypass backoff.
 * v1.2.4: I2C error event log and bounded mode-verified odd/even probe.
 * v1.2.5: Clear stale HAL error flags before each blocking transfer.
 * v1.2.6: Pre-call and first-error I2C hardware/transfer-phase snapshots.
 * v1.2.7: Guard blocking I2C transfers against low SCL or SDA.
 * v1.2.8: Restore and verify the lamp immediately after a failed write.
 * v1.2.9: Reassert mode after enable and record wake/alignment reads.
 * v1.3.0: Stage lamp wake with outputs disabled before loading PWM.
 * v1.3.1: One 50 ms idle-bus retry after failed lamp write/restore. */
#define G0B1_FW_VERSION_MAJOR 1u
#define G0B1_FW_VERSION_MINOR 3u
#ifdef RGB_BENCH_INJECTION
#define G0B1_FW_VERSION_PATCH 3u /* Bench fault-injection image only. */
#else
#define G0B1_FW_VERSION_PATCH 5u
#endif

/* single-register encoding: major*10000 + minor*100 + patch  (max 6.55.35) */
#define G0B1_FW_VERSION_ENC \
    (G0B1_FW_VERSION_MAJOR*10000u + G0B1_FW_VERSION_MINOR*100u + G0B1_FW_VERSION_PATCH)

#endif /* FW_VERSION_H */
