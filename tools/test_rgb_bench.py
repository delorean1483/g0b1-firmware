#!/usr/bin/env python3
"""Host tests for rgb_bench.py frame builders.

Run:  python3 tools/test_rgb_bench.py      (stdlib only)
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rgb_bench  # noqa: E402
from bl_flash import modbus_crc16  # noqa: E402


class FrameTests(unittest.TestCase):
    def test_fc16_frame_red(self):
        f = rgb_bench.fc16_frame(1, 75, [255, 0, 0])
        body = bytes([0x01, 0x10, 0x00, 0x4B, 0x00, 0x03, 0x06,
                      0x00, 0xFF, 0x00, 0x00, 0x00, 0x00])
        self.assertEqual(f[:-2], body)
        crc = modbus_crc16(body)
        self.assertEqual(f[-2:], bytes([crc & 0xFF, crc >> 8]))

    def test_fc03_frame_status_block(self):
        f = rgb_bench.fc03_frame(1, 75, 4)
        self.assertEqual(f[:-2], bytes([0x01, 0x03, 0x00, 0x4B, 0x00, 0x04]))
        crc = modbus_crc16(f[:-2])
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


if __name__ == "__main__":
    unittest.main()
