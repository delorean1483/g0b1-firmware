#!/usr/bin/env python3
"""Host tests for rgb_bench.py frame builders.

Run:  python3 tools/test_rgb_bench.py      (stdlib only)
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rgb_bench  # noqa: E402
from bl_flash import BLError, BLTimeout, modbus_crc16  # noqa: E402


class FrameTests(unittest.TestCase):
    def test_fc16_frame_red(self):
        f = rgb_bench.fc16_frame(1, 75, [255, 0, 0])
        body = bytes([0x01, 0x10, 0x00, 0x4B, 0x00, 0x03, 0x06,
                      0x00, 0xFF, 0x00, 0x00, 0x00, 0x00])
        self.assertEqual(f[:-2], body)
        crc = modbus_crc16(body)
        self.assertEqual(f[-2:], bytes([crc & 0xFF, crc >> 8]))

    def test_diag_steps(self):
        names = [n for n, _ in rgb_bench.DIAG_STEPS]
        self.assertEqual(names, ["off", "red", "green", "blue", "yellow",
                                 "cyan", "magenta", "white", "off"])
        colors = [c for _, c in rgb_bench.DIAG_STEPS]
        self.assertEqual(colors, [(0, 0, 0), (255, 0, 0), (0, 255, 0),
                                  (0, 0, 255), (255, 255, 0), (0, 255, 255),
                                  (255, 0, 255), (255, 255, 255), (0, 0, 0)])

    def test_ramp_steps(self):
        steps = rgb_bench.ramp_steps("green")
        self.assertEqual(len(steps), 17)
        self.assertEqual(steps[0], (0, 0, 0))
        self.assertEqual(steps[1], (0, 17, 0))
        self.assertEqual(steps[-2], (0, 255, 0))
        self.assertEqual(steps[-1], (0, 0, 0))


def crc(body: bytes) -> bytes:
    c = modbus_crc16(body)
    return body + bytes([c & 0xFF, c >> 8])


class FakeSer:
    def __init__(self, reply: bytes):
        self.reply = bytearray(reply)
        self.written = b""

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written += data

    def flush(self):
        pass

    def read(self, n):
        out, self.reply = bytes(self.reply[:n]), self.reply[n:]
        return out


class FakeLink:
    def __init__(self, reply: bytes):
        self.ser = FakeSer(reply)


GOOD = crc(bytes([1, 0x10, 0x00, 0x4B, 0x00, 0x03]))


class WriteColorTests(unittest.TestCase):
    def run_write(self, reply):
        link = FakeLink(reply)
        rgb_bench.write_color(link, (255, 128, 0))
        return link

    def test_happy_path_bytes_written(self):
        link = self.run_write(GOOD)
        self.assertEqual(link.ser.written, crc(bytes(
            [1, 0x10, 0, 0x4B, 0, 3, 6, 0, 255, 0, 128, 0, 0])))

    def test_no_reply(self):
        with self.assertRaisesRegex(BLTimeout, "no reply"):
            self.run_write(b"")

    def test_short_reply(self):
        with self.assertRaisesRegex(BLError, "short/garbled"):
            self.run_write(GOOD[:4])
        with self.assertRaisesRegex(BLError, "short/garbled"):
            self.run_write(GOOD[:1])

    def test_exception_reply(self):
        with self.assertRaisesRegex(BLError, "exception 0x03"):
            self.run_write(crc(bytes([1, 0x90, 0x03])))

    def test_exception_bad_crc(self):
        bad = bytearray(crc(bytes([1, 0x90, 0x03])))
        bad[-1] ^= 0xFF
        with self.assertRaisesRegex(BLError, "CRC mismatch"):
            self.run_write(bytes(bad))

    def test_short_exception(self):
        with self.assertRaisesRegex(BLError, "short/garbled"):
            self.run_write(bytes([1, 0x90, 0x03]))

    def test_bad_crc(self):
        bad = bytearray(GOOD)
        bad[-1] ^= 0xFF
        with self.assertRaisesRegex(BLError, "CRC mismatch"):
            self.run_write(bytes(bad))

    def test_wrong_slave_fc_start_quantity(self):
        for body in (bytes([2, 0x10, 0, 0x4B, 0, 3]),
                     bytes([1, 0x06, 0, 0x4B, 0, 3]),
                     bytes([1, 0x10, 0, 0x4C, 0, 3]),
                     bytes([1, 0x10, 0, 0x4B, 0, 2])):
            with self.assertRaisesRegex(BLError, "unexpected FC 0x10 reply"):
                self.run_write(crc(body))


if __name__ == "__main__":
    unittest.main()
