#!/usr/bin/env bash
# build-slots.sh — produce the two per-slot app binaries for the A/B OTA
# scheme (Task 12).
#
# THIS SCRIPT DOES NOT RUN IN CI OR IN THIS DEV ENVIRONMENT. The app is a
# STM32CubeIDE-managed project (no committed Makefile/CMakeLists for the
# on-target build) — CubeIDE generates its Debug/Release build tree
# (Debug/makefile, Debug/objects.mk, Debug/sources.mk, etc.) only when you
# build the project from the IDE (or `headless-build.sh` via the CubeIDE CLI,
# which needs the IDE's installed toolchain + .cproject to drive it). This
# script documents/automates the exact bench steps once that tree exists; do
# not expect `make` to succeed here.
#
# ---------------------------------------------------------------------------
# Bench procedure:
#
#   1. Build the CubeIDE project ONCE from the IDE (Project > Build Project,
#      Release config) so `cube/Release/makefile` (CubeIDE's generated
#      recursive make wrapper) and the full objects.mk/sources.mk exist.
#      This script re-links with each slot's linker script substituted for
#      `-T ../STM32G0B1RETX_FLASH.ld` — it does NOT recompile every .c, so a
#      normal IDE build must have produced up-to-date .o files first.
#
#   2. Run this script from the `cube/` directory:
#        cd cube && ./build-slots.sh
#
#   3. It re-links the existing Release .o files twice — once against
#      STM32G0B1RETX_SLOTA.ld, once against STM32G0B1RETX_SLOTB.ld — using
#      the same arm-none-eabi-gcc invocation CubeIDE used for the final link
#      step (read it out of Release/makefile's LDFLAGS if this drifts), then
#      objcopy -O binary each resulting .elf.
#
#   4. Verify: `objdump -h g0b1-apu-slotA.elf | grep isr_vector` shows a VMA
#      of 0x08008000 (0x08040000 for slot B); each .bin is well under the
#      224 KB (0x38000) slot budget (expected ~60-70 KB for this app).
#
# ---------------------------------------------------------------------------
set -euo pipefail

TOOLCHAIN_BIN="${TOOLCHAIN_BIN:-/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin}"
CC="${TOOLCHAIN_BIN}/arm-none-eabi-gcc"
OBJCOPY="${TOOLCHAIN_BIN}/arm-none-eabi-objcopy"
OBJDUMP="${TOOLCHAIN_BIN}/arm-none-eabi-objdump"
SIZE="${TOOLCHAIN_BIN}/arm-none-eabi-size"

RELEASE_DIR="${RELEASE_DIR:-Release}"      # CubeIDE's generated build config dir
CPU_FLAGS="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft --specs=nano.specs"

if [ ! -d "${RELEASE_DIR}" ]; then
    echo "ERROR: ${RELEASE_DIR}/ not found — build the CubeIDE project" \
         "(IDE: Project > Build Project) at least once first, so its .o" \
         "files exist for this script to re-link. See the header comment" \
         "in this file for the full bench procedure." >&2
    exit 1
fi

# All object files the Release build produced (App/services + Core + HAL +
# Drivers), gathered the same way CubeIDE's generated makefile does.
# Portable across bash 3.2 (macOS default, no `mapfile`) and bash 4+.
# Object paths never contain newlines, so a while-read loop is safe.
OBJS=()
while IFS= read -r _obj; do OBJS+=("${_obj}"); done \
    < <(find "${RELEASE_DIR}" -name '*.o')
if [ "${#OBJS[@]}" -eq 0 ]; then
    echo "ERROR: no .o files under ${RELEASE_DIR}/ — CubeIDE build didn't" \
         "run, or produced them elsewhere." >&2
    exit 1
fi

build_slot() {
    local slot="$1" ld="$2" base="$3"
    local elf="g0b1-apu-slot${slot}.elf"
    local bin="g0b1-apu-slot${slot}.bin"

    echo "== linking slot ${slot} (${ld}, base ${base}) =="
    "${CC}" ${CPU_FLAGS} -T "${ld}" -Wl,--gc-sections -Wl,-Map="g0b1-apu-slot${slot}.map" \
        -o "${elf}" "${OBJS[@]}" -lc -lm -lnosys

    "${OBJCOPY}" -O binary "${elf}" "${bin}"

    echo "-- slot ${slot} .isr_vector placement --"
    "${OBJDUMP}" -h "${elf}" | grep -A1 isr_vector || true
    echo "-- slot ${slot} size --"
    "${SIZE}" "${elf}"
    echo "-- slot ${slot} .bin size (must be < 224K = 229376 bytes) --"
    wc -c "${bin}"
}

build_slot A STM32G0B1RETX_SLOTA.ld 0x08008000
build_slot B STM32G0B1RETX_SLOTB.ld 0x08040000

echo "Done: g0b1-apu-slotA.bin (@ 0x08008000), g0b1-apu-slotB.bin (@ 0x08040000)"
