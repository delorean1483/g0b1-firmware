#!/usr/bin/env python3
"""gobi_bench.py — EF-G0B1R bench bring-up over Modbus RTU (Tier 0 + control probe).

One tool for the RS-485 link bring-up:
  1. FC 0x11 Report Slave ID  -> expect "EF-G0B1R" (proves cable/baud/address)
  2. FC 0x03 read of the live telemetry registers, decoded to units/labels
  3. optional FC 0x06 write of reg 10 (mode) to exercise the control path

Speaks Modbus RTU directly (computes its own CRC) — only needs pyserial, so it
runs on the Variscite or a laptop. Independent of gobi-agent.

Examples:
    ./gobi_bench.py                          # id check + read all telemetry
    ./gobi_bench.py --port /dev/ttyUSB0
    ./gobi_bench.py --set-mode climate       # write reg 10 = 1, then re-read
                                             # (SAFE on a standalone board; on a
                                             #  real unit this ACTUATES — beware)

Requires: pyserial   ->   pip install pyserial
"""
import argparse
import sys

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed. Run:  pip install pyserial")

# ── enum labels (mirror App/services/control.h) ────────────────────────────────
MODE   = {0: "off", 1: "climate", 2: "battery"}
STATUS = {0: "off", 1: "warming_up", 2: "starting", 3: "running",
          4: "defrost", 5: "charging", 6: "cooling", 7: "chillin"}
ERROR  = {0: "none", 1: "low_oil", 2: "high_engine_temp", 3: "low_battery",
          4: "ac_low_pressure", 5: "ac_high_pressure", 6: "starting_failure",
          7: "standby", 8: "engine_stalled", 9: "no_rpm", 10: "high_ac_pressure"}
OILCHG = {0: "good", 1: "change_soon", 2: "change_needed", 3: "past_due", 4: "dismissed"}
MODE_ARG = {"off": 0, "climate": 1, "battery": 2}

# Telemetry registers to read: (wire addr, label, decoder). Wire addr = fw reg - 1.
def _i16(v):  return v - 0x10000 if v >= 0x8000 else v          # signed degF
def _cv(v):   return f"{v/100.0:.2f} V"
TELEMETRY = [
    ( 0, "cabin_temp",     lambda v: f"{_i16(v)} F"),
    (50, "ext_temp",       lambda v: f"{_i16(v)} F"),
    ( 5, "battery",        _cv),
    (37, "rpm",            lambda v: f"{v}"),
    ( 6, "oil_pressure_ok",lambda v: "yes" if v else "no"),
    ( 7, "ignition",       lambda v: "on" if v else "off"),
    ( 9, "mode",           lambda v: f"{MODE.get(v, '?')} ({v})"),
    (21, "engine_status",  lambda v: f"{STATUS.get(v, '?')} ({v})"),
    (22, "control_status", lambda v: f"{STATUS.get(v, '?')} ({v})"),
    (16, "error",          lambda v: f"{ERROR.get(v, '?')} ({v})"),
    (17, "oil_change",     lambda v: f"{OILCHG.get(v, '?')} ({v})"),
    (10, "engine_hrs",     lambda v: f"{v}"),
    (20, "machine_hrs",    lambda v: f"{v}"),
    (13, "clmt_setpoint",  lambda v: f"{v} F"),
]


def modbus_crc(data: bytes) -> bytes:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


class ModbusError(Exception):
    pass


def _txn(ser, addr, fc, body=b""):
    """Send one RTU request, return the response payload after (addr,fc). Raises
    ModbusError on timeout, CRC error, wrong address, or a Modbus exception."""
    req = bytes([addr, fc]) + body
    req += modbus_crc(req)
    ser.reset_input_buffer()
    ser.write(req)
    ser.flush()

    hdr = ser.read(2)
    if len(hdr) < 2:
        raise ModbusError("no response (timeout) — swap A/B, check 9600 8N1 / GND")
    r_addr, r_fc = hdr[0], hdr[1]
    if r_fc & 0x80:
        rest = ser.read(3)                         # exc code + CRC
    elif r_fc in (0x03, 0x04):
        n = ser.read(1); rest = n + ser.read((n[0] if n else 0) + 2)
    elif r_fc == 0x06:
        rest = ser.read(6)                         # addr(2)+val(2)+CRC(2)
    elif r_fc == 0x11:
        n = ser.read(1); rest = n + ser.read((n[0] if n else 0) + 2)
    else:
        rest = ser.read(64)
    frame = hdr + rest

    if len(frame) < 4 or modbus_crc(frame[:-2]) != frame[-2:]:
        raise ModbusError(f"bad/short frame or CRC mismatch: {frame.hex(' ')}")
    if r_addr != addr:
        raise ModbusError(f"reply from addr {r_addr}, expected {addr}")
    if r_fc & 0x80:
        raise ModbusError(f"Modbus exception 0x{frame[2]:02X}")
    return frame[2:-2]


def report_slave_id(ser, addr):
    payload = _txn(ser, addr, 0x11)                # payload = [count][id bytes]
    return payload[1:1 + payload[0]].decode("ascii", "replace")


def read_reg(ser, addr, wire):
    payload = _txn(ser, addr, 0x03, bytes([wire >> 8, wire & 0xFF, 0, 1]))
    return (payload[1] << 8) | payload[2]          # [bytecount][hi][lo]


def write_reg(ser, addr, wire, value):
    _txn(ser, addr, 0x06, bytes([wire >> 8, wire & 0xFF, value >> 8, value & 0xFF]))


def main() -> int:
    ap = argparse.ArgumentParser(description="EF-G0B1R Modbus bench bring-up")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--addr", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--set-mode", choices=MODE_ARG,
                    help="write reg 10 (0=off/1=climate/2=battery) then re-read — ACTUATES a real unit")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, bytesize=serial.EIGHTBITS,
                            parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                            timeout=args.timeout)
    except serial.SerialException as e:
        print(f"FAIL: cannot open {args.port}: {e}")
        return 2

    with ser:
        # 1. Identity
        try:
            sid = report_slave_id(ser, args.addr)
        except ModbusError as e:
            print(f"FAIL (Report Slave ID): {e}")
            return 1
        ok = (sid == "EF-G0B1R")
        print(f"slave {args.addr}: id = {sid!r}   {'OK' if ok else '(unexpected!)'}")
        if not ok:
            print("  link works but wrong device — check the bus / slave address")

        # 2. Optional control write
        if args.set_mode:
            val = MODE_ARG[args.set_mode]
            try:
                write_reg(ser, args.addr, 9, val)   # reg 10 -> wire 9
                print(f"\nwrote reg 10 (mode) = {val} ({args.set_mode})")
            except ModbusError as e:
                print(f"FAIL (write reg 10): {e}")
                return 1

        # 3. Telemetry
        print("\n--- telemetry ---")
        fails = 0
        for wire, label, dec in TELEMETRY:
            try:
                v = read_reg(ser, args.addr, wire)
                print(f"  {label:<16} {dec(v)}")
            except ModbusError as e:
                fails += 1
                print(f"  {label:<16} ERR ({e})")
        print("\n" + ("PASS — link up, registers decoding." if ok and not fails
                       else "check the failures above."))
    return 0 if ok and not fails else 1


if __name__ == "__main__":
    sys.exit(main())
