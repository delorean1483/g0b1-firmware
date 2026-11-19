# AUTOTERM 0.2.4 / G0B1 1.3.17 build provenance

Review builds only. No flash, heater START, service change or main-branch merge.

Source bases:
- L011: PR #21, feat/l011-0.2.3-bootloader, 1f49c41948faed8e072382047051ef6688629481.
- G0B1: PR #20, integration/heater-v2-1.3.15, 1fc098078abdf2f94f07f7fec9c85627850c7726.
- Standalone L011: PR #1, integration/map-v2-0.2.3, 459d992900d529caefc79e66e8e1cc5cd8f01d1a.
The G0B1 branch also synchronizes the L011 0.2.3 interface/loader prerequisites
from PR #21 so that its own monorepo builds and tests use the complete paired source.

Compiler: arm-none-eabi-gcc, Arm GNU Toolchain 14.3.Rel1 (Build arm-14.174),
14.3.1 20250623. Existing -Os / section-GC settings retained.
L011: LAYOUT=bootloader, no AT_POWER_MODE_TEST. Two independent clean object
directories reproduce the BIN; the standalone repository reproduces it too.
G0B1: existing 1.3.16 bench configuration, HEATER_BENCH_FAULTS compiled in but
one-shot hooks remain disarmed on reset; RGB_BENCH_INJECTION is absent.
These are bench-review artifacts, not a fleet deployment.

| Image | SHA-256 (BIN) | Flash | RAM |
|---|---|---:|---:|
| L011 0.2.4 relocated | 28ec034769efe820a4ad1e6abf7f689e6e2d7333bc8eca596340cc487650b719 | 11,944 B | 1,520 B |
| G0B1 1.3.17 slot A | 9b87ae172162011149b8b38f2127206c454ffd349ca632dc5e6cb42239f20117 | 56,260 B | 26,772 B |
| G0B1 1.3.17 slot B | 128d780ec0e0ac195d7bdfc97fa73b50fafdc639a903755e4f64317a07d0faa0 | 56,260 B | 26,772 B |

L011 HEX SHA-256: ede3b6645f47886ced1fbec4b149beb0abe1c9221322464cf67d6668eb13490e.
L011 vectors: 0x08001000. Application budget: 12,160 B, leaving 216 B.
L011 RAM figure includes the 512-byte stack reservation; retained loader RAM
is separately reserved by the linker. Slot A vectors: 0x08008000, B: 0x08040000.

Validation:
- Entire G0B1 Tests/CMakeLists target manifest: 90/90 test executables passed
  under native GCC 13.3 on Ubuntu, -Wall -Wextra -Werror -funsigned-char;
  heater targets and sensors guard use ASan/UBSan as specified by CMake.
- L011 subset: 119 checks, including 32 register/NV checks. All passed.
- New G0B1 / old L011: no mode writes without capability, applied mode reads 0,
  mode writes refused, old temperature controls unchanged.
- Old 1.3.16 G0B1 heater client linked against new L011 core: 20/20 checks passed,
  including migrated/default temperature behavior and existing stop/start guards.
- New/new real register-file/FSM pass-through and fake UART settings sequencing
  prove in-run settings changes do not generate another keyed START.
- Bridge interlocks, I2C recovery and RGB regressions passed.
- EEPROM uses a separate validated extension word at +4, preserving the legacy
  type/timeout word for rollback. Blank/corrupt/invalid extension means temperature.

Agent/UI contract: Modbus holding 2230 (wire 2229), RW 0 temperature / 1 power;
readback is applied, not desired. L011 I2C byte 0x1A. CAPS word 0x40..0x41 /
Modbus 70 bit 8 = 0x0100, AUTOTERM only. Level 54 applies in power mode;
setpoint 69 applies in temperature mode. Both are stored in either mode.
All unsupported mode writes return ILLEGAL_VALUE; reads return 0. Maintenance
refuses mode writes. No mode change starts or restarts the heater.

Hardware validation is separate: long holds 10 -> 7 -> 4 -> 1 for 8-10 minutes
each and a mode switch while running. Exact scale/minimum, wait byte and START
suffix meanings remain unconfirmed. The 10-second temperature report remains
in both modes. Keep the 5-second Cortex freshness window.