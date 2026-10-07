# Bench runbook — rebuild the fixed app + validate the full OTA (incl. self-confirm)

**Goal:** get the flash-erase-fix (commit `661e955`) into the *app* image in both
slots and validate the complete A/B OTA cycle on silicon — including
**self-confirm** (TRIAL → COMMITTED), which the earlier run couldn't reach
because the streamed app pre-dated the fix.

**Why this run is needed:** the 2026-09-14 OTA proved the *bootloader* transfer +
erase fix on silicon, but the app that got streamed (v1.1.1) was built *before*
the `drv_iflash` bank-addressing fix, so its own `app_confirm → bootcfg_save`
used the buggy erase and never latched COMMITTED. Rebuilding the app with the fix
closes the loop.

## State this runbook assumes (already true on the bench unit)

- Branch `feat/stm32-bootloader-ab-update` @ `661e955` or later (all fixes committed).
- Unit already hardened: **fixed bootloader** flashed, **`BOOT_LOCK=1`**,
  **`nBOOT0=1`**, slot A active/COMMITTED v1.1.0 (reg 2 = 10100), agent live.
  → boot hardening / provisioning Step 1b is DONE; do NOT redo it.
- ST-LINK/V3SET on HDR1. Variscite @ `192.168.0.86`, RS-485 on `/dev/ttyUSB0`,
  `/tmp/bl_flash.py` present (stdlib-only since 2026-10-07 -- no pyserial needed; pass both slot `.bin`s and it picks the inactive one).
- Refs: `tools/provision_boot.md`, `docs/remote-update.md`, `cube/build-slots.sh`.

## Environment (paste once per shell)

```sh
W=/Users/sungkang/Documents/github/g0b1-firmware.worktrees/stm32-bootloader-ab-update
PROG="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.macos64_2.2.200.202503041107/tools/bin/STM32_Programmer_CLI"
TOOLCHAIN="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.0.202411102158/tools/bin"
VAR=root@192.168.0.86
# NOTE: the CubeProgrammer/toolchain plugin version strings above drift across
# STM32CubeIDE updates — if a path 404s, re-glob:  ls -d /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.*/tools/bin
```

---

## Step 0 — Fresh full-flash backup (always first)

```sh
"$PROG" -c port=SWD mode=HOTPLUG -r 0x08000000 0x80000 "$W/../g0b1-backup-$(date +%Y%m%d-%H%M%S).bin"
```
- [ ] "Data read successfully", file is 524288 bytes.
- [ ] Sanity: `python3 -c "import zlib;d=open('<file>','rb').read();print(len(d), hex(zlib.crc32(d[0x8000:0x8000+39604])&0xffffffff))"` → slot A crc `0xcf672fc3` (or whatever the current active slot is).

**Rollback at any later step:** `"$PROG" -c port=SWD -w <backup> 0x08000000 -v` then `-hardRst`.

---

## Step 1 — Rebuild the app in CubeIDE (Release)

The Release build now compiles the fixed `cube/Core/Src/drv_iflash.c`
(via `iflash_pagemap.h`). The `.cproject`/`.project`/`build-slots.sh` fixes are
committed, so a fresh workspace import builds cleanly.

- [ ] Open workspace `bootloader-ws`, project `g0b1-apu (in cube)`.
      (If re-importing: File → Import → Existing Projects → select `cube/`, in place.)
- [ ] **Project → Build Project** (Release). Expect **0 errors** (2 warnings OK:
      unused `MX_IWDG_Init`, RWX LOAD segment).
- [ ] Confirm it's fresh: `ls -la "$W/cube/Release/g0b1-apu.elf"` newer than your last edit.

> If it fails to find `types.h`/`bsp_io.h` or link `app_confirm_*`/`mbp_boot_*`,
> the workspace is stale — Close→Open the project (reloads `.project` links) and
> rebuild. Those gaps are already fixed in git (`3b555d7`); a fresh import has them.

---

## Step 2 — Cut the fixed slot binaries

```sh
cd "$W/cube"
find Release -name '* [0-9].*' -delete            # macOS iCloud dup-object cleanup (recurring)
./build-slots.sh
```
- [ ] Slot A `.isr_vector` VMA `0x08008000`, slot B `0x08040000`.
- [ ] Each `.bin` well under 224 KB (≈ 40 KB).
- [ ] These now contain the erase fix. (`g0b1-apu-slotA.bin` / `g0b1-apu-slotB.bin`.)

---

## Step 3 — OTA-stream the fixed app to the inactive slot + verify SELF-CONFIRM

Currently slot A is active. INFO will report inactive = **B**.

```sh
scp "$W/cube/g0b1-apu-slotB.bin" $VAR:/tmp/g0b1-apu-slotB.bin
ssh $VAR 'systemctl stop gobi-agent; python3 /tmp/bl_flash.py --port /dev/ttyUSB0 --image /tmp/g0b1-apu-slotB.bin'
```
- [ ] `PASS: version changed 10100 -> 10101. Slot B is now active`.

**Verify the swap + page-0 safety + self-confirm** (this is the new part):
```sh
"$PROG" -c port=SWD mode=HOTPLUG -r32 0x08000000 0x4 -r32 0xE000ED08 0x4   # page0 intact + running slot
sleep 10                                                                    # let self-confirm run (>=5 s healthy)
"$PROG" -c port=SWD mode=HOTPLUG -r32 0x08078000 0x30                       # boot-config
```
Decode the boot-config words (little-endian):
- word@+0 = `42434647`, word@+4 low16 = version, **high16 = active_slot (1 = B)**
- word@+12 low8 = slot A state, **word@+24 low8 = slot B state**
- word@+28 = slot B length (0x9AB4 = 39604), word@+32 = slot B crc (`0x449D2715`)

- [ ] `0x08000000` = `20023FE0` — **bootloader intact** (erase hit slot B, not page 0).
- [ ] `VTOR` = `0x08040000` — running slot B.
- [ ] active_slot = **B (1)**.
- [ ] **slot B state = COMMITTED (2)** ← the fix working: self-confirm latched.
      (Before the app fix it stayed TRIAL (1). If still 1 after 10 s, the app
      image is still pre-fix — recheck Step 1/2.)
- [ ] `ssh $VAR 'systemctl start gobi-agent; sleep 4; grep -o "\"apu_fw_version\":[0-9]*" /var/lib/ecofleet/latest.json'` → `10101`.

---

## Step 4 — (Recommended) second OTA to put the fix in slot A too + prove role-swap

Now slot A is inactive and still holds the pre-fix v1.1.0. Stream the fixed
image again so BOTH slots carry the fix and to validate the A/B role-swap.

```sh
scp "$W/cube/g0b1-apu-slotA.bin" $VAR:/tmp/g0b1-apu-slotA.bin   # same fixed build; INFO picks the slot
ssh $VAR 'systemctl stop gobi-agent; python3 /tmp/bl_flash.py --port /dev/ttyUSB0 --image /tmp/g0b1-apu-slotA.bin'
```
- [ ] `PASS` (INFO reports inactive = A this time).
- [ ] After 10 s: boot-config active_slot = **A (0)**, slot A state = **COMMITTED (2)**.
- [ ] `0x08000000` still `20023FE0` (page 0 intact).
- [ ] `systemctl start gobi-agent`; agent reads the streamed version live.

Both slots now run the fixed app; the unit self-confirms; role-swap proven.

---

## Step 5 — Failure / recovery cases (the remaining bench validation)

Backup in hand + BOOT_LOCK set → all of these are brick-safe. See
`docs/remote-update.md` §8 for detail. Run each, confirm the unit always ends on
a good, booting slot:

- [ ] **Interrupted transfer:** kill `bl_flash.py` mid-stream (Ctrl-C), then
      `-hardRst`. Confirm it boots the *previous* committed slot (no half-image
      runs; the interrupted slot stays un-committed).
- [ ] **Bad VERIFY CRC:** edit `/tmp/bl_flash.py` `image_crc` to a wrong value
      for one run → expect NAK `BL_ERR_CRC`, no COMMIT, old slot still boots.
- [ ] **Trial never confirms → revert:** stream an app patched to fail
      `app_is_healthy()` (or hold it in error-shutdown) → after `TRIAL_BOOT_LIMIT`
      (3) resets the bootloader reverts to the other COMMITTED slot.
- [ ] **Engine-running refusal:** with the APU in an engine-running/crank op-state,
      `bl_flash.py` enter-bootloader must get **Modbus exception 0x04** (refused),
      no reset. (`mbp_boot.c` safety gate.)

---

## Gotchas (all bit us on 2026-09-14)

- **`nBOOT0` polarity is inverted** from CubeProgrammer's `-ob displ` text on the
  EF-G0B1R: **`nBOOT0=1` = boot main flash**, `nBOOT0=0` = system memory.
- macOS drops **`* 2.o` duplicate objects** into `cube/Release/` (iCloud) — the
  `find … -delete` in Step 2 is mandatory before `build-slots.sh` or the re-link
  hits "multiple definition".
- `build-slots.sh` needs the arm toolchain on `PATH` (`export PATH="$TOOLCHAIN:$PATH"`)
  and to be run from `cube/`.
- `.project` linked-resource changes need CubeIDE **Close → Open Project**, not
  just Build.
- Do the RS-485 work with `gobi-agent` **stopped** (it owns `/dev/ttyUSB0`); restart it after.
