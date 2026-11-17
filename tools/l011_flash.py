"""Run on Cortex with exclusive ownership of the G0B1 Modbus serial port."""
import argparse
from pathlib import Path
import struct
import subprocess
import time
import zlib

from bl_flash import BLError, Link, _strip_and_check_crc, modbus_crc16

MODE, STATUS, LENGTH, DATA = range(2132, 2136)


class UpdateLink(Link):
    def write_words(self, register, values):
        address = register - 1
        if len(values) == 1:
            body = struct.pack(">BBHH", 1, 6, address, values[0])
            expected = body
        else:
            expected = struct.pack(">BBHH", 1, 16, address, len(values))
            body = expected + bytes([len(values) * 2]) + struct.pack(f">{len(values)}H", *values)
        self._send(body)
        reply = self.ser.read(2)
        if len(reply) != 2:
            raise BLError("no write reply")
        rest = self.ser.read(3 if reply[1] & 0x80 else 6)
        if _strip_and_check_crc(reply + rest) != expected:
            raise BLError(f"write to register {register} rejected")

    def status(self):
        self.write_words(MODE, [0xB013])
        return self.read_holding(STATUS - 1, 1)[0]

    def command(self, payload):
        frame = payload + struct.pack("<H", modbus_crc16(payload))
        words = frame + (b"\0" if len(frame) % 2 else b"")
        for _ in range(4):
            try:
                self.write_words(DATA, struct.unpack(f"<{len(words) // 2}H", words))
                self.write_words(LENGTH, [len(frame)])
                self.write_words(MODE, [0xB014])
                time.sleep(0.15)
                state = self.status()
                if state == 0x101 or (payload == b"\x04" and state == 0x200):
                    return
            except BLError:
                pass
        raise BLError(f"L011 command {payload[0]} failed; restart with the same app image")


def update(link, image):
    if len(image) % 4 or not 192 <= len(image) <= 12160:
        raise ValueError("expected a word-aligned relocated L011 app binary")
    sp, pc = struct.unpack_from("<II", image)
    if sp != 0x20000800 or not pc & 1 or not 0x080010C0 <= pc < 0x08001000 + len(image):
        raise ValueError("expected an app linked at 0x08001000")
    for _ in range(8):
        try:
            link.write_words(MODE, [0xB011])
            time.sleep(0.25)
            if link.status() & 0x100:
                break
        except BLError:
            # The bridge can reject entry while the app or shared bus settles.
            # The firmware rechecks all interlocks on every attempt.
            pass
        time.sleep(0.65)
    else:
        raise BLError(
            "L011 loader did not appear. Bridge entry (reg 2132 = 0xB011) "
            "requires L011 phase OFF, zero run request, and verified lamp off. "
            "For a connected but silent heater, save reg 74, write reg 74 = 0 "
            "before 0xB011, and wait for phase OFF. Restore the saved reg 74 "
            "after successful maintenance exit (0xB012)."
        )
    link.command(struct.pack("<BHI", 1, len(image), zlib.crc32(image)))
    for offset in range(0, len(image), 128):
        link.command(struct.pack("<BH", 2, offset) + image[offset:offset + 128].ljust(128, b"\0"))
        print(f"Verified {min(offset + 128, len(image))}/{len(image)} bytes", flush=True)
    link.command(b"\x03")
    link.command(b"\x04")
    for _ in range(20):
        time.sleep(0.2)
        if link.status() == 0x200:
            link.write_words(MODE, [0xB012])
            return
    raise BLError("map-v2 app did not return; G0B1 remains in maintenance mode")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--port", default="/dev/ecofleet-rs485")
    args = parser.parse_args()
    service = subprocess.run(["systemctl", "is-active", "gobi-agent"],
                             capture_output=True, text=True, check=False)
    if service.stdout.strip() != "inactive":
        parser.error("gobi-agent must be inactive to give the updater exclusive serial access")
    link = UpdateLink(args.port)
    try:
        update(link, args.app.read_bytes())
    finally:
        link.close()
