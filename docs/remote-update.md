# Remote firmware update — ops & release contract

Status: **v1.1.0 is the first A/B remote-update-capable release.** This doc is
the frozen contract sub-project #2 (the cortex/gobi-agent delivery pipeline)
consumes: flash map, CRC32 rules, release artifact naming, and the recovery
procedure. The wire protocol itself is frozen in `App/services/bl_proto.h`
(byte-for-byte) — this doc explains *how* it's used, that header is the
source of truth for the *exact bytes*.

See also: `tools/provision_boot.md` (one-time SWD provisioning) and
`tools/bl_flash.py` (reference host flasher / executable spec).

## 1. Flash map (STM32G0B1RET3, 512 KB flash, single-bank)

| Region | Base | Size | Contents |
|---|---|---|---|
| Bootloader | `0x08000000` | 32 KB | `boot/` — never remotely writable |
| Slot A | `0x08008000` | 224 KB | app image, VTOR @ this base |
| Slot B | `0x08040000` | 224 KB | app image, VTOR @ this base |
| Boot-config | `0x08078000` | 32 KB | ping-pong `bootcfg_t` record (pages 240/241 of the 256×2 KB page map) |

Total: 32 + 224 + 224 + 32 = 512 KB, exactly the device's flash. This layout
requires the option-byte `DUAL_BANK=0` (single-bank, linear page indices
0..255) — see `tools/provision_boot.md` step 1. Constants live in
`cube/Core/Inc/g0b1_slots.h` and `App/services/bootcfg.h` (`SLOT_A_BASE`,
`SLOT_B_BASE`, `SLOT_SIZE`) — treat those headers, not this table, as the
compiled truth if they ever drift.

The bootloader **only ever programs the inactive slot**. It never writes
itself and never writes the currently-active/committed slot — a failed or
malicious update can corrupt only the slot that isn't running.

## 2. CRC32 contract

- Algorithm: **CRC-32/IEEE-802.3** (the common "zlib crc32" — reflected
  polynomial `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`).
  On-target: `App/services/crc32.c` (`crc32_compute`/`crc32_update`). On the
  host: Python's `zlib.crc32(data) & 0xFFFFFFFF` — no extra init/xorout
  needed, `zlib.crc32` already applies the same convention.
- Scope: computed over **the raw `.bin` file contents for one slot, exactly
  `length` bytes** (the real file size — not rounded up). The firmware pads
  the *final* 8-byte flash word past `length` with `0xFF` where the file
  size isn't a multiple of 8 (`App/services/bl_session.c:handle_data`), but
  that padding is outside `[0, length)` and is never read back or covered
  by VERIFY's CRC — the host only ever needs `zlib.crc32()` of the file
  exactly as it sits on disk. No header or length prefix is covered by
  the CRC either; `length` travels alongside it as a separate VERIFY field.
- Where it's checked:
  1. **VERIFY** (`bl_proto.h` `BL_SUB_VERIFY`, FC 0x41 sub 0x03): the
     bootloader reads back everything it just programmed into the inactive
     slot and recomputes the CRC over exactly `length` bytes; a mismatch is
     NAK'd with `BL_ERR_CRC` and nothing is committed.
  2. **Every boot decision** (`bootcfg_slot_crc_ok()`, `App/services/bootcfg.c`):
     the bootloader re-reads the *entire* candidate slot (`slot[n].length`
     bytes) and recomputes CRC32 against the stored `slot[n].crc32` before
     it will ever jump to that slot — not just once at VERIFY time. This is
     what makes the "both slots invalid" recovery case (§6, §8) work: a slot
     whose flash content no longer matches its stored CRC is treated as
     unbootable exactly like `SLOT_STATE_BAD`, even if its state field
     still says `COMMITTED`.
  3. **The host tool** should independently `zlib.crc32()` the `.bin` it's
     about to send *before* transmitting VERIFY, so a bad build/download is
     caught locally instead of wasting a full transfer.

## 3. Release artifacts — two `.bin` per release

A release build produces exactly two flat binaries, one per slot, both
built from the **same source tree** against a different linker script
(`cube/STM32G0B1RETX_SLOTA.ld` / `_SLOTB.ld` — FLASH origin `0x08008000` /
`0x08040000`, VTOR set at runtime from `g_pfnVectors`). `cube/build-slots.sh`
documents the exact re-link/objcopy recipe (CubeIDE-managed project — see
that script's header for why it isn't a plain `make`).

Naming convention for the CI/release output (this is what sub-project #2's
OTA payload carries):

```
g0b1-apu-<ver>-slotA.bin
g0b1-apu-<ver>-slotB.bin
```

e.g. for this release: `g0b1-apu-1.1.0-slotA.bin`, `g0b1-apu-1.1.0-slotB.bin`.
`<ver>` matches `G0B1_FW_VERSION_MAJOR.MINOR.PATCH` from
`App/services/fw_version.h` (the same value reported at reg 2, encoded as
`major*10000 + minor*100 + patch`). **Both** slot binaries ship in every
release — same source, same version, but *not* byte-identical: each is
linked at a different FLASH origin (`0x08008000` vs `0x08040000`), so the
vector table and any other absolute-address content differ, and each has
its own CRC32. Which one actually gets flashed is decided at update time
(§4), not at build time — the agent only ever needs the one matching the
device's reported `inactive_slot`. Each `.bin` must be ≤ `APP_SLOT_SIZE`
(224 KB / `0x38000`); `build-slots.sh` step 4 asserts this.

## 4. How the agent picks the inactive-slot `.bin`

The device always knows which slot is *inactive* — that's the only one the
bootloader will ever program. The host-side flow (implemented in
`tools/bl_flash.py`, to be re-implemented by the cortex agent in
sub-project #2):

1. Command enter-bootloader (write holding reg 35 = `0x00A5`; refused with
   Modbus exception 0x04 while the engine is running — see §7 item 5).
2. Wait for the device to reset and come back on the bus in bootloader mode.
3. Send `INFO` (FC 0x41 sub 0x01). The 12-byte reply body
   (`App/services/bl_proto.c:bl_build_info`) includes `inactive_slot` (0 =
   A, 1 = B) and `chunk_max` (240) — **this is the authoritative answer**,
   never inferred from reg 2 or guessed client-side.
4. The agent picks `g0b1-apu-<ver>-slotA.bin` or `-slotB.bin` to match
   whichever `inactive_slot` INFO reported, and streams that one file.

Because roles swap every successful update (COMMIT flips `active_slot`),
consecutive updates alternate A→B→A→B — the agent must re-query INFO each
time rather than caching which slot was inactive last time.

## 5. SWD provisioning

One-time, bench-only, done before a unit ever leaves the shop (or during
in-field recovery with an ST-LINK in hand). Full procedure:
**`tools/provision_boot.md`**. Summary: set `DUAL_BANK=0`, flash the
bootloader + both slot `.bin`s at their fixed addresses, seed a boot-config
record marking Slot A `COMMITTED` + active. After this, reg 2 reads the
provisioned version and the device is remote-update-capable from then on —
no further SWD access should be required for normal updates.

## 6. Recovery procedure

The design goal: **a bricked app should never require an ST-LINK.** Two
independent recovery paths exist, layered so if one fails to trigger you
still have the other:

**A. Automatic (A/B trial/revert, no operator action).** Every update
COMMITs the new slot as `TRIAL`, not `COMMITTED`. The freshly-booted app
must self-confirm — `App/services/app_confirm.c` marks the slot
`COMMITTED` after `APP_CONFIRM_HEALTHY_SECS` (5s) of continuous healthy
ticks. If the new app never reaches that (crashes, watchdog-loops, never
calls in healthy) the *bootloader's* boot decision
(`App/services/boot_decision.c`) increments `trial_count` on every power-up
that still finds the active slot `TRIAL`; once `trial_count >
TRIAL_BOOT_LIMIT` (3), it marks that slot `BAD` and falls back to the other
slot if it's `COMMITTED` and its CRC checks out. No RS-485, no operator,
no SWD — this is the normal-path safety net and should be exercised as
bench failure-case 3, "trial never confirms" (§8).

**B. Forced re-stream (operator-initiated, still no SWD).** If both slots
are `BAD` (or otherwise fail their CRC check — bench failure-case 4, "both
slots invalid", §8), the
bootloader has nowhere to jump and stays in **update mode** by itself: the
RS-485 bus is live and `bl_flash.py` (or the agent) can re-stream a slot
exactly as in a normal update, using either the reg-35 command if the app
side is somehow still alive, or simply power-cycling and letting the
bootloader fall into update mode on its own (`boot_decide()` returns
`ENTER_UPDATE` whenever neither slot is bootable). This is the core
recoverability claim of the design — SWD is a fallback for provisioning
and diagnosis, not a required recovery path for the field.

**C. SWD (last resort).** If the RS-485 bus itself is dead (wiring fault,
dead UART, etc.) or the boot-config region is corrupted such that it isn't
recoverable from the RS-485 path, fall back to `tools/provision_boot.md` —
re-flash a slot and/or reseed the boot-config with an ST-LINK.

## 7. Wire protocol (summary — `bl_proto.h` is authoritative)

Standard Modbus RTU framing (`[addr][fc]...[crc16 lo][crc16 hi]`), slave
address fixed at 1 in both app and bootloader modes.

- App side (normal Modbus): reg 2 (holding, read-only) = encoded version;
  reg 35 (holding, write-only) — write `0x00A5` to arm enter-bootloader;
  refused with exception `0x04` (`MB_EXC_SLAVE_DEVICE_FAILURE`) while
  `engine_running()` is true (`App/services/mbp_boot.c`).
- Bootloader side (FC `0x41` control / `0x42` data — reserved, unimplemented
  by the app): `INFO` / `ERASE` / `VERIFY` / `COMMIT` / `ABORT` / `STATUS`
  control sub-functions, plus 240-byte `DATA` chunks. Exact byte layouts,
  field widths, and endianness are in the `bl_proto.h` header comment —
  match it byte-for-byte; `tools/bl_flash.py` is the reference
  implementation.
- Failure signaling: NAK frames (`[addr][fc|0x80][err]`) carry one of
  `BL_ERR_STATE` / `BL_ERR_RANGE` / `BL_ERR_CRC` / `BL_ERR_FLASH`.

## 8. Bench validation status

Bootloader + app logic is host-unit-tested (73/73, `Tests/`) but the
following are **hardware-only** and are the user's to execute on the bench,
not reproducible in this environment:

- SWD provisioning (`tools/provision_boot.md`).
- The happy-path RS-485 round-trip (`tools/bl_flash.py`) end-to-end,
  including role-swap on a second update.
- The five failure/recovery cases: interrupted transfer, bad VERIFY CRC,
  trial-never-confirms revert, both-slots-invalid safe recovery, and
  engine-running refusal (`tools/bl_flash.py`'s header comment repeats this
  list next to the code it exercises).

## 9. How this flows into a cortex OTA (sub-project #2)

This plan freezes the **device-side contract**: the flash map, the CRC32
rule, the two-`.bin` release shape, and the RS-485 wire protocol
(`bl_proto.h`). Sub-project #2 is expected to:

1. Package both `g0b1-apu-<ver>-slot{A,B}.bin` files (§3) into the cortex
   `.swu` payload (or an equivalent side-channel) alongside a manifest
   naming the new version.
2. On the Variscite/gobi-agent side, implement the same host-flasher flow
   §4 documents and `tools/bl_flash.py` proves out: enter-bootloader → INFO
   → pick the matching `.bin` by `inactive_slot` → ERASE → stream DATA →
   VERIFY (independently CRC32-checked before sending) → COMMIT → confirm
   reg 2 changed.
3. Gate the update on the same safety rule the firmware itself enforces
   (§6/§7 — refuse while the engine is running) at the agent layer too, so
   the operator gets a clear UI-level reason rather than relying solely on
   the Modbus exception.
4. Authenticity/integrity above CRC32 (e.g. signature verification) is
   inherited from the existing signed `.swu` pipeline
   (`SWUPDATE_SIGN_KEY`, see the OTA distribution pipeline notes) — CRC32
   here only protects against transfer corruption, it is not a substitute
   for that signature.
