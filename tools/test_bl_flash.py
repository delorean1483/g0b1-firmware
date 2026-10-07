#!/usr/bin/env python3
"""Host tests for bl_flash.py against a fake EF-G0B1R on a pty.

Run:  python3 tools/test_bl_flash.py      (stdlib only -- no pyserial)

The fake speaks just enough of both protocols (app-mode Modbus reg 2 / reg 35,
bootloader FC 0x41/0x42 from App/services/bl_proto.h) to drive bl_flash.run()
end to end over a real tty, so the termios serial layer is exercised too.
"""
import argparse
import contextlib
import io
import os
import sys
import threading
import time
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bl_flash  # noqa: E402

SLOT_SIZE = 0x38000


def make_image(base: int, n: int = 600) -> bytes:
    """A fake slot .bin: initial SP, then a Thumb reset vector inside `base`."""
    body = bytes((i * 7) & 0xFF for i in range(n - 8))
    return (0x20023FE0).to_bytes(4, "little") + (base + 0x101).to_bytes(4, "little") + body


class FakeDevice:
    """App + bootloader on the far end of a pty. `mode` is "app" or "bl"."""

    def __init__(self, mode="app", version=10105, new_version=10106, inactive_slot=1):
        self.mode = mode
        self.version = version
        self.new_version = new_version
        self.inactive_slot = inactive_slot
        self.erased = False
        self.entered = 0
        self.written = bytearray()
        self.committed = False
        self.master, self._slave = os.openpty()
        bl_flash.make_raw(self.master)
        self.slave_path = os.ttyname(self._slave)
        # Keep our slave fd open: on Linux a master read returns EIO whenever
        # no slave fd is open, which would kill the reader thread.
        self._stop = False
        self._buf = bytearray()
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

    def close(self):
        self._stop = True
        self._t.join(1)
        os.close(self.master)
        os.close(self._slave)

    def _reply(self, body: bytes):
        os.write(self.master, bl_flash._with_crc(body))

    def _frame_len(self):
        b = self._buf
        if len(b) < 2:
            return None
        fc = b[1]
        if fc in (0x03, 0x06):
            return 8
        if fc == 0x41:
            if len(b) < 3:
                return None
            return 13 if b[2] == bl_flash.BL_SUB_VERIFY else 5
        if fc == 0x42:
            return None if len(b) < 7 else 7 + b[6] + 2
        return len(b)  # junk: swallow

    def _run(self):
        import select
        while not self._stop:
            r, _, _ = select.select([self.master], [], [], 0.02)
            if not r:
                continue
            try:
                self._buf += os.read(self.master, 512)
            except OSError:
                return
            while True:
                n = self._frame_len()
                if n is None or len(self._buf) < n:
                    break
                frame, self._buf = bytes(self._buf[:n]), self._buf[n:]
                self._handle(frame[:-2])

    def _handle(self, req: bytes):
        fc = req[1]
        if self.mode == "app":
            if fc == 0x03:
                self._reply(bytes([1, 3, 2]) + self.version.to_bytes(2, "big"))
            elif fc == 0x06 and int.from_bytes(req[2:4], "big") == 34:  # reg 35
                self.entered += 1
                self.mode = "bl"          # resets; no reply
            else:
                self._reply(bytes([1, fc | 0x80, 0x01]))
            return
        # bootloader mode: only FC 0x41/0x42
        if fc == 0x41:
            sub = req[2]
            if sub == bl_flash.BL_SUB_INFO:
                self._reply(bytes([1, 0x41, sub, 1, self.inactive_slot])
                            + SLOT_SIZE.to_bytes(4, "big") + (240).to_bytes(2, "big") + bytes([1]))
            elif sub == bl_flash.BL_SUB_ERASE:
                self.erased = True
                self.written = bytearray()
                self._reply(bytes([1, 0x41, sub, 0]))
            elif sub == bl_flash.BL_SUB_VERIFY:
                length = int.from_bytes(req[3:7], "big")
                crc = int.from_bytes(req[7:11], "big")
                ok = length == len(self.written) and crc == zlib.crc32(self.written) & 0xFFFFFFFF
                self._reply(bytes([1, 0x41, sub, 0]) if ok else bytes([1, 0xC1, 3]))
            elif sub == bl_flash.BL_SUB_COMMIT:
                self._reply(bytes([1, 0x41, sub, 0]))
                self.committed = True
                self.version = self.new_version
                self.inactive_slot ^= 1
                self.mode = "app"
            else:
                self._reply(bytes([1, 0x41, sub, 0]))
        elif fc == 0x42:
            off = int.from_bytes(req[2:6], "big")
            data = req[7:7 + req[6]]
            if off != len(self.written):
                self._reply(bytes([1, 0xC2, 2]))
                return
            self.written += data
            self._reply(bytes([1, 0x42]) + off.to_bytes(4, "big"))
        else:
            self._reply(bytes([1, fc | 0x80, 0x01]))  # bootloader rejects FC 0x03


class BlFlashTest(unittest.TestCase):
    def setUp(self):
        self.tmp = []
        self.img_a = self._write(make_image(bl_flash.SLOT_A_BASE), "slotA.bin")
        self.img_b = self._write(make_image(bl_flash.SLOT_B_BASE), "slotB.bin")

    def tearDown(self):
        for p in self.tmp:
            os.unlink(p)

    def _write(self, data, name):
        p = f"/tmp/test_bl_flash_{os.getpid()}_{name}"
        with open(p, "wb") as f:
            f.write(data)
        self.tmp.append(p)
        return p

    def _run(self, dev, images, **kw):
        args = argparse.Namespace(port=dev.slave_path, image=images, baud=9600, timeout=0.5,
                                  reset_wait=0.05, max_retries=2, dry_run=False)
        for k, v in kw.items():
            setattr(args, k, v)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            try:
                rc = bl_flash.run(args)
            except bl_flash.BLError as e:
                print(f"FAIL: {e}")
                rc = 1
        return rc, out.getvalue()

    def test_no_pyserial_dependency(self):
        self.assertNotIn("serial", sys.modules, "bl_flash must be stdlib-only")

    def test_picks_matching_image_for_inactive_slot(self):
        for inactive, want in ((1, self.img_b), (0, self.img_a)):
            dev = FakeDevice(inactive_slot=inactive)
            try:
                rc, out = self._run(dev, [self.img_a, self.img_b])
                self.assertEqual(rc, 0, out)
                self.assertIn("PASS", out)
                with open(want, "rb") as f:
                    self.assertEqual(bytes(dev.written), f.read())
            finally:
                dev.close()

    def test_wrong_slot_single_image_refused_before_erase(self):
        dev = FakeDevice(inactive_slot=1)
        try:
            rc, out = self._run(dev, [self.img_a])
            self.assertEqual(rc, 1, out)
            self.assertIn("inactive slot is B", out)
            self.assertFalse(dev.erased)
        finally:
            dev.close()

    def test_resumes_when_device_already_in_bootloader(self):
        dev = FakeDevice(mode="bl", inactive_slot=1)
        try:
            rc, out = self._run(dev, [self.img_a, self.img_b])
            self.assertEqual(rc, 0, out)
            self.assertEqual(dev.entered, 0)
            self.assertTrue(dev.committed)
            self.assertIn("10106", out)
        finally:
            dev.close()

    def test_dry_run_changes_nothing(self):
        dev = FakeDevice()
        try:
            rc, out = self._run(dev, [self.img_a, self.img_b], dry_run=True)
            self.assertEqual(rc, 0, out)
            self.assertEqual(dev.entered, 0)
            self.assertIn("10105", out)
        finally:
            dev.close()

    def test_rejects_image_linked_for_neither_slot(self):
        bad = self._write(make_image(0x08000000), "boot.bin")
        dev = FakeDevice()
        try:
            rc, out = self._run(dev, [bad], dry_run=True)
            self.assertNotEqual(rc, 0, out)
            self.assertEqual(dev.entered, 0)
        finally:
            dev.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
