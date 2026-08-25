#!/usr/bin/env python3
"""report_slave_id.py — bench link check for the EF-G0B1R APU over Modbus RTU.

Sends Modbus function 0x11 (Report Slave ID) and prints the device ID string.
A reply of "EF-G0B1R" proves the RS-485 wiring, baud, and slave address are all
correct — independent of gobi-agent (which still needs its own baud/map fix).

The EF-G0B1R firmware answers FC 0x11 with:  addr 0x11 count "EF-G0B1R" CRC
(no run-indicator byte — see App/services/mb_engine.c:handle_report_slave_id).

Usage:
    ./report_slave_id.py                       # /dev/ttyUSB0, 9600 8N1, addr 1
    ./report_slave_id.py --port /dev/ttyUSB0
    ./report_slave_id.py --port /dev/cu.usbserial-XXXX   # macOS uses cu.* / tty.*

Requires: pyserial   ->   pip install pyserial
"""
import argparse
import sys

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed. Run:  pip install pyserial")


def modbus_crc(data: bytes) -> bytes:
    """Modbus RTU CRC16 (poly 0xA001, init 0xFFFF), returned low byte first."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])  # lo, hi (wire order)


def main() -> int:
    ap = argparse.ArgumentParser(description="EF-G0B1R Modbus Report Slave ID check")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--addr", type=int, default=1, help="Modbus slave address (default 1)")
    ap.add_argument("--timeout", type=float, default=1.0, help="read timeout seconds")
    args = ap.parse_args()

    req = bytes([args.addr, 0x11])
    req += modbus_crc(req)

    try:
        ser = serial.Serial(args.port, args.baud, bytesize=serial.EIGHTBITS,
                            parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                            timeout=args.timeout)
    except serial.SerialException as e:
        print(f"FAIL: cannot open {args.port}: {e}")
        return 2

    with ser:
        ser.reset_input_buffer()
        ser.write(req)
        ser.flush()
        print(f"TX  {req.hex(' ')}   (addr={args.addr} fc=0x11 Report Slave ID)")

        # Staged read so we finish as soon as the frame is complete.
        hdr = ser.read(2)                     # addr, function
        if len(hdr) == 2 and (hdr[1] & 0x80):
            rest = ser.read(3)                # exception: code + CRC
        elif len(hdr) == 2 and hdr[1] == 0x11:
            cnt = ser.read(1)                 # byte count
            rest = cnt + ser.read((cnt[0] if cnt else 0) + 2)   # id bytes + CRC
        else:
            rest = ser.read(64)               # unexpected — grab what we can
        resp = hdr + rest

    if not resp:
        print("FAIL: no response (timeout).")
        print("  hints: swap A/B (TX_A <-> RX_B); confirm 9600 8N1; check GND_485 wire")
        return 1

    print(f"RX  {resp.hex(' ')}")

    if len(resp) < 5:
        print(f"FAIL: short/garbled frame ({len(resp)} bytes) — likely A/B swap or wrong baud.")
        return 1

    body, rx_crc = resp[:-2], resp[-2:]
    if modbus_crc(body) != rx_crc:
        print("FAIL: CRC mismatch — A/B swap, line noise, or baud mismatch.")
        return 1

    addr, fc = resp[0], resp[1]
    if addr != args.addr:
        print(f"FAIL: reply from addr {addr}, expected {args.addr}.")
        return 1
    if fc == (0x11 | 0x80):
        print(f"FAIL: device returned Modbus exception 0x{resp[2]:02X} to FC 0x11.")
        return 1
    if fc != 0x11:
        print(f"FAIL: unexpected function 0x{fc:02X} in reply.")
        return 1

    count = resp[2]
    text = resp[3:3 + count].decode("ascii", errors="replace")
    print(f"\nPASS: slave {addr} reports ID = {text!r}  ({count} bytes)")
    if text == "EF-G0B1R":
        print("      ✓ EF-G0B1R confirmed — RS-485 link, baud, and address all good.")
    else:
        print("      link works, but ID != expected 'EF-G0B1R' — check you're on the right bus.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
