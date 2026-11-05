#!/usr/bin/env python3
"""Bench tool for the external RGB lamp on the EF-G0B1R (Modbus regs 76-79).

Stdlib only; runs on the Variscite (reuses the serial layer in bl_flash.py).
STOP gobi-agent FIRST:  systemctl stop gobi-agent   (start it again after).
The agent owns the serial port and its restore logic would put the saved
color back over the test colors.

  rgb_bench.py [--port /dev/ttyUSB0] set R G B
  rgb_bench.py read
  rgb_bench.py diag [--step-s 1.5]
  rgb_bench.py ramp red|green|blue

Registers (firmware numbering; wire address = firmware register - 1):
  76 led_red, 77 led_green, 78 led_blue (RW 0-255), 79 led_status (RO:
  bit 0 present, bit 1 last apply OK, bit 2 pending).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bl_flash import (BAUD, SLAVE_ADDR, BLError, Link, SerialError,  # noqa: E402
                      modbus_crc16)

WIRE_RED = 75   # firmware reg 76
REG_COUNT = 4   # red, green, blue, status

DIAG_STEPS = [
    ("off", (0, 0, 0)),
    ("red", (255, 0, 0)),
    ("green", (0, 255, 0)),
    ("blue", (0, 0, 255)),
    ("yellow", (255, 255, 0)),
    ("cyan", (0, 255, 255)),
    ("magenta", (255, 0, 255)),
    ("white", (255, 255, 255)),
    ("off", (0, 0, 0)),
]

CHANNELS = {"red": 0, "green": 1, "blue": 2}
RAMP_STEPS = 16  # values 0, 17, ... 255


def _with_crc(body: bytes) -> bytes:
    crc = modbus_crc16(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def fc16_frame(slave: int, wire_start: int, values: list) -> bytes:
    n = len(values)
    body = bytes([slave, 0x10, (wire_start >> 8) & 0xFF, wire_start & 0xFF,
                  (n >> 8) & 0xFF, n & 0xFF, 2 * n])
    for v in values:
        body += bytes([(v >> 8) & 0xFF, v & 0xFF])
    return _with_crc(body)


def fc03_frame(slave: int, wire_start: int, count: int) -> bytes:
    return _with_crc(bytes([slave, 0x03, (wire_start >> 8) & 0xFF, wire_start & 0xFF,
                            (count >> 8) & 0xFF, count & 0xFF]))


def ramp_steps(channel: str) -> list:
    idx = CHANNELS[channel]
    steps = []
    for i in range(RAMP_STEPS):
        rgb = [0, 0, 0]
        rgb[idx] = i * 17
        steps.append(tuple(rgb))
    steps.append((0, 0, 0))
    return steps


def write_color(link: Link, rgb) -> None:
    """FC16 write of 76-78 in one request (one lamp update)."""
    link.ser.reset_input_buffer()
    link.ser.write(fc16_frame(SLAVE_ADDR, WIRE_RED, list(rgb)))
    link.ser.flush()
    reply = link.ser.read(8)
    if len(reply) < 5:
        raise BLError("timeout waiting for FC 0x10 reply")
    if reply[1] & 0x80:
        raise BLError(f"Modbus exception 0x{reply[2]:02X} on FC 0x10 write")
    if len(reply) < 8:
        raise BLError("short FC 0x10 reply")
    crc = modbus_crc16(reply[:-2])
    if reply[-2:] != bytes([crc & 0xFF, crc >> 8]):
        raise BLError(f"CRC mismatch in reply: {reply.hex(' ')}")


def read_state(link: Link):
    """Returns ((r, g, b), status) as read back from 76-79."""
    r, g, b, st = link.read_holding(WIRE_RED, REG_COUNT)
    return (r, g, b), st


def describe_status(st: int) -> str:
    return (f"present={st & 1} apply_ok={(st >> 1) & 1} pending={(st >> 2) & 1}"
            f" (0x{st:04X})")


def show(link: Link, label: str) -> None:
    rgb, st = read_state(link)
    print(f"{label:<10} applied R={rgb[0]:3d} G={rgb[1]:3d} B={rgb[2]:3d}  "
          f"{describe_status(st)}", flush=True)


def run_steps(link: Link, steps, step_s: float) -> None:
    for name, rgb in steps:
        write_color(link, rgb)
        time.sleep(step_s)
        show(link, name)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", default="/dev/ttyUSB0")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("set", help="write a color (0-255 each)")
    for c in "RGB":
        s.add_argument(c, type=int, choices=range(256), metavar=c)
    sub.add_parser("read", help="read applied color and status")
    d = sub.add_parser("diag", help="off, primaries, secondaries, white, off")
    d.add_argument("--step-s", type=float, default=1.5)
    r = sub.add_parser("ramp", help="ramp one channel 0-255 in 16 steps, then off")
    r.add_argument("channel", choices=sorted(CHANNELS))
    r.add_argument("--step-s", type=float, default=1.5)
    args = ap.parse_args(argv)

    print("stop gobi-agent first (systemctl stop gobi-agent): it owns the port "
          "and its restore would undo test colors", file=sys.stderr)
    try:
        link = Link(args.port, BAUD)
    except SerialError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    try:
        if args.cmd == "set":
            write_color(link, (args.R, args.G, args.B))
            time.sleep(0.3)
            show(link, "set")
        elif args.cmd == "read":
            show(link, "read")
        elif args.cmd == "diag":
            run_steps(link, DIAG_STEPS, args.step_s)
        else:
            steps = ramp_steps(args.channel)
            run_steps(link, [(f"{args.channel}{rgb[CHANNELS[args.channel]]}", rgb)
                             for rgb in steps], args.step_s)
    except BLError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
