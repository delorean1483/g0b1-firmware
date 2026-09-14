# SWD provisioning — one-time bootloader + A/B slot setup

This is the **one-time, bench-only** setup that turns a blank STM32G0B1RET3
into a remote-update-capable unit: set the flash-partitioning option byte,
flash the bootloader and both app slots, and seed a valid boot-config so the
device has something to boot on power-up. After this, `tools/bl_flash.py`
handles all further updates over RS-485 — no SWD needed again unless the
recovery procedures in `docs/remote-update.md` §6 escalate to it.

Hardware: **ST-LINK/V3SET on header HDR1**. Tooling: **STM32CubeProgrammer**
(`STM32_Programmer_CLI` on `PATH`). Read `docs/remote-update.md` §1
(flash map) and §2 (CRC32 contract) first — this doc assumes both.

> The exact `STM32_Programmer_CLI` flag spelling (`-w` vs `-d`/`--download`,
> `-ob` sub-syntax) has drifted slightly across CubeProgrammer releases.
> Run `STM32_Programmer_CLI --help` and `STM32_Programmer_CLI -c port=SWD -ob
> --help` once on your installed version and adjust the commands below if
> they don't match — the addresses, option-byte name, and the boot-config
> byte layout are the parts that must not change; the CLI invocation is
> whatever your installed version accepts.

---

## Step 1 — Set the single-bank option byte (DUAL_BANK=0)

The STM32G0B1's 512 KB flash can be split as dual-bank (two independent
256 KB banks, page indices reset at each bank) or single-bank (one linear
512 KB region, page indices `0..255`). This project's flash map (§1 of
`docs/remote-update.md`) assumes **linear** addressing — `drv_iflash`'s page
math (`App/services/` on-target driver) and the boot-config's fixed pages
240/241 are only correct in single-bank mode. **This step is mandatory and
only needs doing once per chip** (it survives reflashing; it does not
survive a full chip erase back to factory option bytes).

```sh
STM32_Programmer_CLI -c port=SWD -ob DUAL_BANK=0
```

Confirm the read-back:

```sh
STM32_Programmer_CLI -c port=SWD -ob displ
```

Look for `DUAL_BANK: 0` (or equivalent "single bank" wording — the exact
label depends on the CubeProgrammer version) in the printed option-byte
table. If the chip was previously dual-bank, this option write triggers an
internal flash-layout change and the target resets; reconnect (`-c
port=SWD`) before continuing.

⚠️ Do this **before** step 2. Flashing images while dual-bank is still set
will land at the wrong physical addresses relative to this project's
single-bank map.

---

## Step 1b — Force boot from main flash (mandatory)

The unit must **never** boot the STM32 system-memory (ST ROM) bootloader.
BOOT0 is on **PA14/SWCLK**, exposed only at the SWD header, and two situations
can otherwise latch the chip into ST ROM: a cold power-on with the debugger
attached, or a main flash that reads empty (e.g. mid-recovery). Once in ST ROM,
the ordinary RS-485 traffic on the bus (the agent's Modbus polling) can be
mis-parsed by ROM's USART autobaud/command parser and **corrupt flash** — bench
2026-09-14, this erased a unit's bootloader and left it unbootable until an SWD
re-flash. Lock boot to main flash so ST ROM is unreachable:

```sh
# Ignore the BOOT0/SWCLK pin and select main-flash boot.
STM32_Programmer_CLI -c port=SWD -ob nBOOT_SEL=1 nBOOT0=1
# Hard-lock: always boot 0x08000000, even if flash reads empty (ROM/SRAM
# boot become impossible). SWD flashing still works at RDP level 0.
STM32_Programmer_CLI -c port=SWD -ob BOOT_LOCK=1
STM32_Programmer_CLI -c port=SWD -ob displ   # confirm nBOOT0=1, BOOT_LOCK=1
```

> ⚠️ **`nBOOT0` polarity gotcha on the EF-G0B1R:** empirically **`nBOOT0=1`
> boots main flash** and **`nBOOT0=0` boots system memory** — the *inverse* of
> what STM32CubeProgrammer's `-ob displ` text implies (it prints the raw
> `nBOOT1` sentence literally). Trust this table, not the printed sentence.
> With `BOOT_LOCK=1` the point is moot (boot is forced to flash regardless),
> but set `nBOOT0=1` too so a future `BOOT_LOCK` clear still boots flash.

Reverting `BOOT_LOCK` later requires an RDP level 1→0 regression (mass-erase),
so only clear it on a bench unit you intend to fully re-provision. SWD memory
read/write and `-ob` reads work normally while it's set.

---

## Step 2 — Flash the bootloader and both app slots

Build the bootloader first if you haven't:

```sh
make -C boot        # produces boot/build/g0b1-boot.elf + g0b1-boot.bin
```

Build both app slot binaries per `cube/build-slots.sh` (CubeIDE-managed
project — see that script's header comment for the exact bench recipe: it
re-links the IDE's own Release `.o` files against
`STM32G0B1RETX_SLOTA.ld` / `_SLOTB.ld`). You should end up with
`g0b1-apu-slotA.bin` and `g0b1-apu-slotB.bin` (renamed to
`g0b1-apu-1.1.0-slotA.bin` / `-slotB.bin` for release, per
`docs/remote-update.md` §3 — either name is fine to flash from, only the
address matters here).

Flash all three at their fixed addresses:

```sh
STM32_Programmer_CLI -c port=SWD \
  -w boot/build/g0b1-boot.bin        0x08000000 \
  -w cube/g0b1-apu-slotA.bin         0x08008000 \
  -w cube/g0b1-apu-slotB.bin         0x08040000 \
  -v
```

(`-v` verifies each write by reading back. If your CubeProgrammer version
doesn't accept multiple `-w` pairs in one invocation, run three separate
`STM32_Programmer_CLI -c port=SWD -w <file> <addr> -v` commands instead —
functionally identical.)

Do **not** flash anything at `0x08078000` (boot-config) yet — that's step 3.

---

## Step 3 — Seed the boot-config

The bootloader refuses to jump anywhere until it has a *valid* `bootcfg_t`
record (magic + version + a CRC32 over the record itself — see
`App/services/bootcfg.c:record_valid()`). A freshly-erased chip has neither
boot-config page valid, so `boot_decide()` returns `ENTER_UPDATE` and the
unit sits in bootloader/update mode forever without one. Two ways to get a
valid record onto the device; **Option A is recommended** — it's
deterministic about which slot ends up active, Option B has a documented
quirk (below).

### Option A (recommended): write a precomputed 48-byte record

`bootcfg_t` (`App/services/bootcfg.h`) is a fixed 48-byte struct:

| Offset | Field | Value to seed (Slot A, COMMITTED, active) |
|---|---|---|
| 0 | `magic` (u32) | `0x42434647` |
| 4 | `version` (u16) | `1` |
| 6 | `active_slot` (u16) | `0` (`SLOT_A`) |
| 8 | `seq` (u32) | `1` |
| 12 | `slot[0].state` (u8) + 3 pad | `2` (`SLOT_STATE_COMMITTED`) |
| 16 | `slot[0].length` (u32) | exact byte size of the flashed `slotA.bin` |
| 20 | `slot[0].crc32` (u32) | CRC-32/IEEE-802.3 (zlib) of that exact `slotA.bin`, same convention as `docs/remote-update.md` §2 |
| 24 | `slot[1].state` (u8) + 3 pad | `0` (`SLOT_STATE_BAD` — Slot B untrusted until it's actually streamed by an update) |
| 28 | `slot[1].length` (u32) | `0` |
| 32 | `slot[1].crc32` (u32) | `0` |
| 36 | `trial_count` (u16) | `0` |
| 38 | `_pad` (u16) | `0` |
| 40 | `_pad2` (u32) | `0` |
| 44 | `crc32` (u32) | CRC-32/IEEE-802.3 (zlib) over **bytes 0..43** of this same record (`crc32_compute`, see `App/services/crc32.c` / `bootcfg.c:CRC_OFF`) |

`length`/`crc32` for slot A are per-build — generate the record with a short
script rather than hand-computing them (this also gets the endianness right:
every multi-byte field is written **little-endian**, native to
`STM32_Programmer_CLI -w` of a raw file):

```python
#!/usr/bin/env python3
import struct, sys, zlib

img = open(sys.argv[1], "rb").read()          # e.g. g0b1-apu-1.1.0-slotA.bin
img_crc = zlib.crc32(img) & 0xFFFFFFFF

body = struct.pack(
    "<IHHI" "BBBBII" "BBBBII" "HHI",
    0x42434647, 1, 0, 1,                       # magic, version, active_slot=A, seq
    2, 0, 0, 0, len(img), img_crc,              # slot A: state=COMMITTED + pad, length, crc32
    0, 0, 0, 0, 0, 0,                           # slot B: state=BAD + pad, length=0, crc32=0
    0, 0, 0,                                    # trial_count, _pad, _pad2
)
assert len(body) == 44
crc = zlib.crc32(body) & 0xFFFFFFFF
record = body + struct.pack("<I", crc)
assert len(record) == 48
open("bootcfg-seed.bin", "wb").write(record)
print(f"wrote bootcfg-seed.bin: slotA length={len(img)} crc32=0x{img_crc:08X}")
```

Save that snippet as `make_bootcfg_seed.py`, run it against the exact
`slotA.bin` you flashed in step 2, then write the result at the boot-config
base:

```sh
python3 make_bootcfg_seed.py cube/g0b1-apu-slotA.bin
STM32_Programmer_CLI -c port=SWD -w bootcfg-seed.bin 0x08078000 -v
```

(Only page 240 needs a valid record — page 241 stays erased/`0xFF`, which
`record_valid()` correctly rejects; `bootcfg_load()` falls back to whichever
page *is* valid, so a single seeded page is sufficient. The next
`bootcfg_save()`, e.g. from a future update's COMMIT or from
`app_confirm`'s self-confirm, naturally starts the normal page 240/241
ping-pong from there.)

### Option B (fallback): let the bootloader fall into recovery once

If you'd rather not run a script, power-cycle right after step 2 with no
boot-config seeded. `bootcfg_load()` finds neither page valid,
`boot_decide()` returns `ENTER_UPDATE`, and the bootloader comes up in
update mode with the RS-485 bus live — run `tools/bl_flash.py` once to
stream an image, exactly like a normal field update.

⚠️ **Known asymmetry, read before relying on this path:** `bl_session_init()`
computes the *target* slot as "whichever slot isn't `cfg->active_slot`". On
a freshly-zeroed boot-config, `active_slot` reads as `0` = `SLOT_A` by
default (it's just zero-initialized memory, not a real "Slot A is active"
fact) — so the session's target ends up **Slot B**, not Slot A, on this very
first stream. In other words: Option B, run once, will actually commit and
boot from **Slot B**, even though this doc (and the boot-config `bootcfg_t`
table above) frame the "normal" seeded state as Slot A active. Functionally
this is harmless — the two slots are symmetric, and the device ends up
running a real, self-confirmed image either way — but if you specifically
need Slot A to be the one marked active (e.g. to match a fleet convention,
or before recording which slot is "first" for a specific unit), run Option
B, let it settle (self-confirm ~5 s after boot), and then run
`tools/bl_flash.py` a **second** time — INFO will report Slot A as
inactive and the second update will move the active slot back to A. Or just
use Option A, which sets it correctly on the first try.

---

## Step 4 — Power-cycle and confirm

Disconnect the ST-LINK (or at minimum, let the target run — don't hold it
halted; the IWDG is live outside a debug session) and power-cycle the unit.

Expected: the unit boots the app in Slot A (Option A) or Slot B (Option B,
first run), and reg 2 reads the provisioned version — the *encoded* value
`major*10000 + minor*100 + patch`, so for `1.1.0` expect **`10100`**
decimal. Confirm the link first, then read reg 2 (any Modbus RTU client,
9600 8N1, slave addr 1, FC 0x03 starting at wire address 1, count 1 — or
use `tools/bl_flash.py`'s `read_reg2()` helper directly):

```sh
python3 tools/report_slave_id.py --port <ftdi-port>     # sanity: link/baud/addr
```

From here, the unit is provisioned. All further updates go through
`tools/bl_flash.py` over RS-485 — see `docs/remote-update.md`.
