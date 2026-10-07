#!/usr/bin/env python3
"""bl_flash.py — reference host flasher for the EF-G0B1R STM32 A/B bootloader.

Modeled on report_slave_id.py's Modbus-RTU idiom (same staged-read style,
same inline CRC16), but stdlib-only: the serial port is driven with termios
directly (SerialPort below), so it runs on the Variscite image, which ships
python3 without pyserial. Speaks two protocols over the same
RS-485 link, both defined byte-for-byte in App/services/bl_proto.h (that
header is the frozen source of truth -- this file must match it exactly):

  1. Normal Modbus RTU (slave addr 1, 9600 8N1) to the *app*: FC 0x03 read
     reg 2 (version), FC 0x06 write reg 35 = 0x00A5 (enter-bootloader).
     NOTE: both reg 35 (enter-bootloader) and reg 34 (reset) call
     NVIC_SystemReset() *inside* the write handler and never return --
     the app resets before it can send a Modbus ACK for that write. A
     write to reg 35 that gets no reply at all is the SUCCESS case. A
     reply IS sent only when the device refuses (Modbus exception 0x04,
     engine running -- App/services/mbp_boot.c).

  2. Bootloader session protocol (same slave addr, same baud, once the
     device has reset into update mode): FC 0x41 CONTROL (INFO / ERASE /
     VERIFY / COMMIT / ABORT / STATUS sub-functions) and FC 0x42 DATA
     (240-byte chunks). See App/services/bl_proto.h's header comment for
     the exact wire layout of every frame; App/services/bl_session.c for
     the state machine (ERASE -> N x DATA -> VERIFY -> COMMIT, each step
     gated on the previous one having succeeded).

CRC32 (image integrity, VERIFY step): CRC-32/IEEE-802.3, i.e. Python's
zlib.crc32() with no extra init/xorout massaging needed -- see
docs/remote-update.md Sec.2 for why that matches App/services/crc32.c's
convention on-target.

Usage:
    # validate the images + confirm the link/current version, no device changes:
    ./bl_flash.py --port /dev/ttyUSB0 --image g0b1-apu-slotA.bin g0b1-apu-slotB.bin --dry-run

    # real update (device must be OFF -- engine not running, see refusal case
    # below; on the Variscite stop gobi-agent first, it owns the port). Pass
    # both slot .bins: after INFO reports the inactive slot, the image whose
    # reset vector is linked for that slot is streamed (the filename is never
    # trusted, see docs/remote-update.md Sec.4). A single --image also works
    # and is refused before ERASE if it is linked for the other slot.
    ./bl_flash.py --port /dev/ttyUSB0 --image g0b1-apu-slotA.bin g0b1-apu-slotB.bin

    # If a previous run left the device sitting in the bootloader (it answers
    # FC 0x03 with exception 0x01), just re-run: the session is resumed from
    # INFO without the enter-bootloader write.

BENCH-DEFERRED (hardware-only, not exercised by this file or by the host
test suite -- these are the user's to run on real silicon, see
docs/remote-update.md Sec.8 and the design's Phase 6 bench plan):
  - The happy-path round trip itself (this script against a real device),
    including confirming role-swap (re-run picks the other slot next time).
  - The five failure/recovery cases: (1) interrupted transfer (kill this
    script mid-stream, power-cycle, confirm the old slot still boots);
    (2) deliberately wrong --crc32-override at VERIFY -> expect NAK
    BL_ERR_CRC, no commit; (3) a patched image that never self-confirms ->
    TRIAL_BOOT_LIMIT reverts to the old slot; (4) both slots SWD-corrupted
    -> bootloader stays in update mode with no ST-LINK needed, re-stream
    recovers it; (5) enter-bootloader while the engine is running -> Modbus
    exception 0x04, no reset (see enter_bootloader() below).

Requires: Python 3 stdlib only (POSIX termios -- Linux or macOS).
"""
import argparse
import os
import select
import sys
import termios
import time
import zlib


# ── constants (mirror App/services/bl_proto.h, modbus_defs.h, mbp_boot.h) ──
SLAVE_ADDR = 1
BAUD = 9600

REG_VERSION = 2          # holding, read-only, FC 0x03
REG_ENTER_BOOTLOADER = 35  # holding, write-only, FC 0x06
ENTER_BOOTLOADER_VALUE = 0x00A5

BL_FC_CONTROL = 0x41
BL_FC_DATA = 0x42
BL_SUB_INFO = 0x01
BL_SUB_ERASE = 0x02
BL_SUB_VERIFY = 0x03
BL_SUB_COMMIT = 0x04
BL_SUB_ABORT = 0x05
BL_SUB_STATUS = 0x06

BL_ERR_NAMES = {1: "BL_ERR_STATE", 2: "BL_ERR_RANGE", 3: "BL_ERR_CRC", 4: "BL_ERR_FLASH"}
BL_CHUNK_DEFAULT = 240  # BL_CHUNK_MAX; INFO's reported chunk_max is authoritative

# Inactive-slot flash bases (mirror g0b1_slots.h / bl_proto.h). Used only to
# sanity-check that the given --image is linked for the slot INFO reports
# inactive -- a wrong-slot image flashes fine but boots into garbage because
# its vector table points into the other slot's flash region.
SLOT_A_BASE = 0x08008000
SLOT_B_BASE = 0x08040000
SLOT_SIZE = 0x38000     # 224 KB; INFO's reported slot_size is authoritative

# ERASE erases all 112 pages of a 224 KB slot SYNCHRONOUSLY on-target before
# it ACKs the FC 0x41 ERASE request (App/services/bl_session.c:erase_target)
# -- roughly 2.5-4.5s at ~22-40ms/page on the G0. The normal per-frame
# --timeout (default 2.0s) is too short for this one reply and would time
# out before the erase even finishes; ERASE (and, defensively, COMMIT, which
# also does a flash write + save before it ACKs) always use this dedicated,
# much longer timeout regardless of --timeout. ERASE is NOT idempotent-cheap
# to retry -- every retry re-erases the whole slot from scratch -- so this
# alone (not a bigger retry count) is the fix for "erase never completes".
ERASE_TIMEOUT_S = 35.0


class BLError(Exception):
    """Any protocol-level failure (malformed frame, bad CRC, exhausted retries)."""


class BLTimeout(BLError):
    """No (complete) reply arrived within the frame timeout."""


class BLNak(BLError):
    """Device explicitly NAK'd the request."""

    def __init__(self, err_code: int):
        self.err_code = err_code
        name = BL_ERR_NAMES.get(err_code, f"unknown(0x{err_code:02X})")
        super().__init__(f"NAK: {name}")


# ── CRC16 (Modbus RTU), poly 0xA001, init 0xFFFF ────────────────────────────
def modbus_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc


def _with_crc(body: bytes) -> bytes:
    crc = modbus_crc16(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def _strip_and_check_crc(frame: bytes) -> bytes:
    if len(frame) < 3:
        raise BLTimeout(f"short frame: {frame.hex(' ')}")
    body, rx_crc = frame[:-2], frame[-2:]
    crc = modbus_crc16(body)
    if bytes([crc & 0xFF, (crc >> 8) & 0xFF]) != rx_crc:
        raise BLError(f"CRC mismatch in reply: {frame.hex(' ')}")
    return body


class SerialError(Exception):
    """The serial device could not be opened or configured."""


def make_raw(fd: int):
    """cfmakeraw() by hand -- the Yocto python3 split leaves out the `tty`
    module, so don't depend on tty.setraw()."""
    a = termios.tcgetattr(fd)
    a[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
              | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON
              | termios.IXOFF | termios.IXANY)
    a[1] &= ~termios.OPOST
    a[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    a[2] &= ~(termios.CSIZE | termios.PARENB)
    a[2] |= termios.CS8
    a[6][termios.VMIN] = 1
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)


class SerialPort:
    """Minimal stdlib stand-in for the slice of pyserial this tool used:
    raw 8N1, `timeout` = total seconds a read(n) may wait (pyserial's meaning),
    read() returns fewer than n bytes on timeout."""

    def __init__(self, path: str, baud: int, timeout: float):
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise SerialError(f"unsupported baud rate {baud}")
        try:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as e:
            raise SerialError(f"cannot open {path}: {e}") from e
        try:
            make_raw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE)
            attrs[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
            if hasattr(termios, "CRTSCTS"):
                attrs[2] &= ~termios.CRTSCTS
            attrs[4] = attrs[5] = speed
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except termios.error as e:
            os.close(self.fd)
            raise SerialError(f"cannot configure {path}: {e}") from e
        self.timeout = timeout

    def close(self):
        os.close(self.fd)

    def reset_input_buffer(self):
        termios.tcflush(self.fd, termios.TCIFLUSH)

    def write(self, data: bytes):
        view = memoryview(data)
        while view:
            try:
                n = os.write(self.fd, view)
            except BlockingIOError:
                select.select([], [self.fd], [], 1.0)
                continue
            view = view[n:]

    def flush(self):
        termios.tcdrain(self.fd)

    def read(self, n: int) -> bytes:
        out = bytearray()
        deadline = time.monotonic() + self.timeout
        while len(out) < n:
            left = deadline - time.monotonic()
            if left <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                break
            try:
                chunk = os.read(self.fd, n - len(out))
            except BlockingIOError:
                continue
            if not chunk:
                break
            out += chunk
        return bytes(out)


def reset_vector_slot(image: bytes):
    """'A' / 'B' for the slot the image is linked for (its Thumb reset vector,
    bytes 4-7), or None if it points into neither slot."""
    vec = int.from_bytes(image[4:8], "little") & ~1
    for letter, base in (("A", SLOT_A_BASE), ("B", SLOT_B_BASE)):
        if base <= vec < base + SLOT_SIZE:
            return letter
    return None


class Link:
    """RS-485 Modbus RTU link to the EF-G0B1R -- app-mode reg access plus the
    bootloader-mode session protocol, on the same serial port/baud (both run
    at 9600 8N1; the bootloader never changes the line settings)."""

    def __init__(self, port: str, baud: int = BAUD, timeout: float = 2.0):
        self.ser = SerialPort(port, baud, timeout)

    def close(self):
        self.ser.close()

    def _send(self, body: bytes):
        frame = _with_crc(body)
        self.ser.reset_input_buffer()
        self.ser.write(frame)
        self.ser.flush()

    # ---- normal Modbus (app mode) -----------------------------------------
    def read_holding(self, wire_start: int, count: int) -> list:
        req = bytes([SLAVE_ADDR, 0x03, (wire_start >> 8) & 0xFF, wire_start & 0xFF,
                     (count >> 8) & 0xFF, count & 0xFF])
        self._send(req)
        hdr = self.ser.read(2)
        if len(hdr) < 2:
            raise BLTimeout("timeout waiting for FC 0x03 reply")
        fc = hdr[1]
        if fc & 0x80:
            rest = self.ser.read(3)
            body = _strip_and_check_crc(hdr + rest)
            raise BLError(f"Modbus exception 0x{body[2]:02X} on FC 0x03 read")
        if fc != 0x03:
            raise BLError(f"unexpected fc 0x{fc:02X} for FC 0x03 request")
        cnt = self.ser.read(1)
        if not cnt:
            raise BLTimeout("timeout reading FC 0x03 byte-count")
        n = cnt[0]
        rest = self.ser.read(n + 2)
        if len(rest) < n + 2:
            raise BLTimeout("timeout mid-frame reading FC 0x03 reply")
        body = _strip_and_check_crc(hdr + cnt + rest)
        data = body[3:3 + n]
        return [(data[2 * i] << 8) | data[2 * i + 1] for i in range(n // 2)]

    def read_reg2(self) -> int:
        return self.read_holding(REG_VERSION - 1, 1)[0]

    def device_in_app(self) -> bool:
        """True if the device answers a normal app-mode register read (FC 0x03)
        -- i.e. it is running the *application*, not the bootloader. Used to
        disambiguate a real bootloader NAK from the app simply rejecting the
        unknown bootloader function code: an app illegal-function reply to
        FC 0x41 is byte-for-byte identical (0x01 0xC1 0x01 ...) to a bootloader
        BL_ERR_STATE NAK, so an INFO 'NAK' alone cannot tell them apart. The
        bootloader rejects FC 0x03 (it only speaks 0x41/0x42), so a successful
        reg read is a positive signal the device never entered update mode."""
        try:
            self.read_reg2()
            return True
        except BLError:
            return False

    def enter_bootloader(self, refusal_timeout: float = 0.3):
        """Write reg 35 = 0x00A5. On success the device resets immediately
        inside the write handler and sends NOTHING back -- a timeout here is
        the expected, successful outcome. A reply only happens on refusal
        (engine running -> Modbus exception 0x04, App/services/mbp_boot.c);
        that case raises so it isn't mistaken for a normal reset."""
        wire_start = REG_ENTER_BOOTLOADER - 1
        req = bytes([SLAVE_ADDR, 0x06, (wire_start >> 8) & 0xFF, wire_start & 0xFF,
                     (ENTER_BOOTLOADER_VALUE >> 8) & 0xFF, ENTER_BOOTLOADER_VALUE & 0xFF])
        self._send(req)
        old_timeout = self.ser.timeout
        self.ser.timeout = refusal_timeout
        try:
            hdr = self.ser.read(2)
        finally:
            self.ser.timeout = old_timeout
        if len(hdr) == 2 and (hdr[1] & 0x80):
            rest = self.ser.read(3)
            body = _strip_and_check_crc(hdr + rest)
            exc = body[2]
            hint = " (engine running?)" if exc == 4 else ""
            raise BLError(f"enter-bootloader REFUSED: Modbus exception 0x{exc:02X}{hint}")
        # else: no reply, or a partial/garbled byte or two racing the reset --
        # both are the normal "it reset" outcome; nothing further to check here.

    # ---- bootloader session protocol --------------------------------------
    def _recv_bl_frame(self) -> bytes:
        """Read one bootloader-mode reply (CRC-checked body returned, CRC
        stripped). See the frame-shape table in bl_proto.h's header comment."""
        hdr = self.ser.read(2)
        if len(hdr) < 2:
            raise BLTimeout("timeout waiting for bootloader reply header")
        fc = hdr[1]
        if fc in (BL_FC_CONTROL | 0x80, BL_FC_DATA | 0x80):
            rest = self.ser.read(3)                       # err(1) + crc16(2)
            if len(rest) < 3:
                raise BLTimeout("timeout mid-frame (NAK)")
            return _strip_and_check_crc(hdr + rest)
        if fc == BL_FC_DATA:
            rest = self.ser.read(6)                        # offset(4) + crc16(2)
            if len(rest) < 6:
                raise BLTimeout("timeout mid-frame (DATA ack)")
            return _strip_and_check_crc(hdr + rest)
        if fc == BL_FC_CONTROL:
            sub_b = self.ser.read(1)
            if len(sub_b) < 1:
                raise BLTimeout("timeout waiting for control sub-code")
            sub = sub_b[0]
            tail_len = {BL_SUB_INFO: 9, BL_SUB_STATUS: 5}.get(sub, 1)  # + crc16(2) below
            rest = self.ser.read(tail_len + 2)
            if len(rest) < tail_len + 2:
                raise BLTimeout("timeout mid-frame (control reply)")
            return _strip_and_check_crc(hdr + sub_b + rest)
        raise BLError(f"unexpected fc 0x{fc:02X} in bootloader reply")

    def _expect_control_ack(self, sub: int, timeout: float = None):
        """Read one CONTROL ack/NAK. `timeout`, if given, temporarily
        overrides the link's normal per-frame timeout for this one read
        (used by ERASE/COMMIT, which can take several seconds on-target --
        see ERASE_TIMEOUT_S) and is always restored afterward."""
        if timeout is None:
            body = self._recv_bl_frame()
        else:
            old_timeout = self.ser.timeout
            self.ser.timeout = timeout
            try:
                body = self._recv_bl_frame()
            finally:
                self.ser.timeout = old_timeout
        if body[1] == (BL_FC_CONTROL | 0x80):
            raise BLNak(body[2])
        if body[1] != BL_FC_CONTROL or len(body) != 4 or body[2] != sub or body[3] != 0x00:
            raise BLError(f"malformed ACK for control sub 0x{sub:02X}: {body.hex(' ')}")

    def bl_info(self) -> dict:
        self._send(bytes([SLAVE_ADDR, BL_FC_CONTROL, BL_SUB_INFO]))
        body = self._recv_bl_frame()
        if body[1] == (BL_FC_CONTROL | 0x80):
            raise BLNak(body[2])
        if len(body) != 12 or body[1] != BL_FC_CONTROL or body[2] != BL_SUB_INFO:
            raise BLError(f"malformed INFO reply: {body.hex(' ')}")
        return {
            "bl_version": body[3],
            "inactive_slot": body[4],                      # 0 = A, 1 = B
            "slot_size": int.from_bytes(body[5:9], "big"),
            "chunk_max": int.from_bytes(body[9:11], "big"),
            "crc_algo": body[11],                           # 1 = CRC32/IEEE
        }

    def bl_erase(self):
        self._send(bytes([SLAVE_ADDR, BL_FC_CONTROL, BL_SUB_ERASE]))
        self._expect_control_ack(BL_SUB_ERASE, timeout=ERASE_TIMEOUT_S)

    def bl_data(self, offset: int, chunk: bytes):
        req = (bytes([SLAVE_ADDR, BL_FC_DATA]) + offset.to_bytes(4, "big")
               + bytes([len(chunk)]) + chunk)
        self._send(req)
        body = self._recv_bl_frame()
        if body[1] == (BL_FC_DATA | 0x80):
            raise BLNak(body[2])
        if body[1] != BL_FC_DATA or len(body) != 6:
            raise BLError(f"malformed DATA ack: {body.hex(' ')}")
        got_off = int.from_bytes(body[2:6], "big")
        if got_off != offset:
            raise BLError(f"DATA ack offset mismatch: sent {offset}, got {got_off}")

    def bl_verify(self, length: int, crc32: int):
        req = (bytes([SLAVE_ADDR, BL_FC_CONTROL, BL_SUB_VERIFY])
               + length.to_bytes(4, "big") + crc32.to_bytes(4, "big"))
        self._send(req)
        self._expect_control_ack(BL_SUB_VERIFY)

    def bl_commit(self):
        self._send(bytes([SLAVE_ADDR, BL_FC_CONTROL, BL_SUB_COMMIT]))
        # Defensively long too: COMMIT does a flash program + bootcfg_save()
        # before it ACKs (App/services/bl_session.c:handle_commit).
        self._expect_control_ack(BL_SUB_COMMIT, timeout=ERASE_TIMEOUT_S)


def _retry(fn, max_retries: int, label: str):
    last_exc = None
    for attempt in range(1, max_retries + 1):
        try:
            return fn()
        except (BLTimeout, BLNak) as e:
            last_exc = e
            print(f"  [{label}] attempt {attempt}/{max_retries} failed: {e} -- retrying")
    raise BLError(f"{label}: exhausted {max_retries} retries (last error: {last_exc})")


def run(args) -> int:
    images = {}                       # slot letter -> (path, bytes)
    for path in args.image:
        with open(path, "rb") as f:
            data = f.read()
        slot = reset_vector_slot(data)
        if slot is None:
            raise BLError(f"{path}: reset vector 0x{int.from_bytes(data[4:8], 'little'):08X} "
                          "is in neither app slot -- not a slot .bin (bootloader image?)")
        if slot in images:
            raise BLError(f"{path} and {images[slot][0]} are both linked for slot {slot}")
        images[slot] = (path, data)
        print(f"image: {path}  slot {slot}  {len(data)} bytes  "
              f"crc32=0x{zlib.crc32(data) & 0xFFFFFFFF:08X}")

    link = Link(args.port, args.baud, timeout=args.timeout)
    try:
        in_bootloader = False
        try:
            old_ver = link.read_reg2()
            print(f"reg2 (current version, encoded) = {old_ver}")
        except BLError as e:
            # The bootloader answers FC 0x03 with exception 0x01. If INFO then
            # answers too, an earlier run left it in update mode: resume.
            if "exception 0x01" not in str(e):
                raise
            try:
                link.bl_info()
            except BLError:
                raise e
            in_bootloader = True
            old_ver = None
            print("device is already in the bootloader (left there by an earlier run); "
                  "resuming the session without enter-bootloader")

        if args.dry_run:
            longest = max(len(d) for _, d in images.values())
            n_chunks = -(-longest // BL_CHUNK_DEFAULT)  # ceil div
            print(f"--dry-run: link OK, images OK. Would stream ~{n_chunks} chunks "
                  f"of up to {BL_CHUNK_DEFAULT} bytes. Not entering the bootloader; "
                  "no device state changed.")
            return 0

        if not in_bootloader:
            print("commanding enter-bootloader (reg 35 = 0x00A5)...")
            link.enter_bootloader()
            print(f"waiting {args.reset_wait}s for reset + bootloader bring-up...")
            time.sleep(args.reset_wait)

        try:
            info = _retry(link.bl_info, args.max_retries, "INFO")
        except BLError as e:
            # An INFO "NAK" (0xC1 / err 0x01) is byte-identical to the *app*
            # rejecting FC 0x41 as an illegal function -- so this frequently
            # means the device never left the app (enter-bootloader did not take
            # effect), NOT a bootloader error. Probe app-mode reg access to tell
            # them apart and report the real cause instead of a misleading NAK.
            if link.device_in_app():
                raise BLError(
                    "enter-bootloader did not take effect -- the device is still "
                    "running the application (it answered a normal register read). "
                    "The INFO 'NAK' was the app rejecting the unknown bootloader "
                    "function code 0x41, not a bootloader error. Check: engine "
                    "confirmed OFF (a running engine refuses the arm)? reg-35 arm "
                    "(0x00A5) reaching the device? Try a longer --reset-wait. "
                    f"(underlying INFO error: {e})") from e
            raise
        slot_letter = "A" if info["inactive_slot"] == 0 else "B"
        print(f"INFO: bl_version={info['bl_version']} inactive_slot={slot_letter} "
              f"slot_size={info['slot_size']} chunk_max={info['chunk_max']} "
              f"crc_algo={info['crc_algo']}")
        if info["crc_algo"] != 1:
            raise BLError(f"unexpected crc_algo {info['crc_algo']} (expected 1 = CRC32/IEEE)")
        if not (0 < info["chunk_max"] <= BL_CHUNK_DEFAULT):
            raise BLError(f"unexpected chunk_max {info['chunk_max']} from device")
        # Slot pick / wrong-slot guard: the .bin's reset vector (bytes 4-7,
        # Thumb bit set) points into the flash region it was LINKED for. The
        # bootloader always writes whichever slot INFO reports inactive, so a
        # .bin linked for the other slot would erase+stream+verify fine but
        # boot into garbage. Pick the matching image, or refuse before ERASE.
        if slot_letter not in images:
            raise BLError(
                f"wrong-slot image: the device's inactive slot is {slot_letter}, but no "
                f"--image is linked for it (got slot {', '.join(sorted(images))}). "
                f"Pass the slot-{slot_letter} .bin (or both). Nothing was erased; the "
                "device is waiting in the bootloader -- re-run to resume.")
        path, image = images[slot_letter]
        image_crc = zlib.crc32(image) & 0xFFFFFFFF
        print(f"streaming {path} (linked for slot {slot_letter})")
        if len(image) > info["slot_size"]:
            raise BLError(f"image ({len(image)} B) exceeds slot size ({info['slot_size']} B)")
        chunk_max = info["chunk_max"]

        print(f"ERASE... (synchronous on-target, can take a few seconds; "
              f"using a {ERASE_TIMEOUT_S}s reply timeout)")
        _retry(link.bl_erase, args.max_retries, "ERASE")

        print(f"streaming {len(image)} bytes in {chunk_max}-byte chunks...")
        off = 0
        while off < len(image):
            chunk = image[off:off + chunk_max]
            _retry(lambda o=off, c=chunk: link.bl_data(o, c), args.max_retries, f"DATA@{off}")
            off += len(chunk)
            print(f"\r  {off}/{len(image)} bytes ({100 * off // len(image)}%)", end="", flush=True)
        print()

        print(f"VERIFY (length={len(image)} crc32=0x{image_crc:08X})...")
        _retry(lambda: link.bl_verify(len(image), image_crc), args.max_retries, "VERIFY")

        print("COMMIT...")
        _retry(link.bl_commit, args.max_retries, "COMMIT")

        print(f"waiting {args.reset_wait}s for the committed slot to boot...")
        time.sleep(args.reset_wait)

        new_ver = _retry(link.read_reg2, args.max_retries, "post-update reg2 read")
        print(f"reg2 (new version, encoded) = {new_ver}")
        if old_ver is None:
            print(f"PASS: device is back in the app reporting {new_ver} (previous "
                  "version unknown -- the run started in the bootloader). "
                  f"Slot {slot_letter} is now active (TRIAL until it self-confirms).")
            return 0
        if new_ver == old_ver:
            print("FAIL: reg2 did not change -- update likely did not take effect.")
            return 1
        print(f"PASS: version changed {old_ver} -> {new_ver}. "
              f"Slot {slot_letter} is now active (TRIAL until it self-confirms; "
              "see App/services/app_confirm.c).")
        return 0
    finally:
        link.close()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Reference RS-485 flasher for the EF-G0B1R A/B bootloader.")
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/ttyUSB0")
    ap.add_argument("--image", required=True, nargs="+",
                     help="slot .bin(s); pass both slot A and slot B builds and the one "
                          "linked for the slot INFO reports inactive is streamed")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--timeout", type=float, default=2.0,
                     help="per-frame reply timeout (s) for INFO/DATA/register reads. "
                          "Does NOT apply to ERASE or COMMIT, which always use a "
                          f"dedicated {ERASE_TIMEOUT_S}s timeout regardless of this "
                          "value -- ERASE erases the whole slot synchronously "
                          "on-target before it ACKs (~2.5-4.5s on the G0)")
    ap.add_argument("--reset-wait", type=float, default=2.0,
                     help="seconds to wait after enter-bootloader / after COMMIT "
                          "for the device to reset and come back on the bus")
    ap.add_argument("--max-retries", type=int, default=5,
                     help="per-step retry budget on timeout/NAK")
    ap.add_argument("--dry-run", action="store_true",
                     help="validate the image + read reg2 only; never enters the "
                          "bootloader and never writes anything to the device")
    args = ap.parse_args()

    try:
        return run(args)
    except FileNotFoundError as e:
        print(f"FAIL: {e}")
        return 2
    except (SerialError, OSError, termios.error) as e:
        print(f"FAIL: serial error: {e}")
        return 2
    except BLError as e:
        print(f"FAIL: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
