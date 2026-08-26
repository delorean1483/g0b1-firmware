#!/usr/bin/env python3
"""climate_monitor.py — drive an op-mode over Modbus and watch the FSM walk.

Sets reg 10 (mode: 0=off/1=climate/2=battery) over Modbus RTU, then polls
mode/engine-status/control-status/error every ~2 s and prints a timestamped
line whenever anything changes. This is the RELIABLE way to validate the
control FSM on real hardware.

  IMPORTANT: validate the control loop over Modbus with the DEBUGGER DETACHED.
  Halting a free-running control loop over SWD (ST-Link connect-under-reset,
  halt/resume cycling, hardware watchpoints) destabilizes the target and
  produces phantom resets / register clears that look like firmware bugs but
  are debugger artifacts. See docs/phase-b-level2-swd.md.

Speaks Modbus RTU directly (self-CRC); only needs pyserial. Runs on the
Variscite (/dev/ttyUSB0) or a laptop (/dev/cu.usbserial-*).

  ./climate_monitor.py                       # climate, 120 s, /dev/ttyUSB0
  ./climate_monitor.py --port /dev/cu.usbserial-XXXX --set climate --secs 90
  ./climate_monitor.py --set off             # command safe-off and watch

Requires: pyserial  ->  pip install pyserial
"""
import sys, time, argparse

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed. Run:  pip install pyserial")

MODE   = {0: "off", 1: "climate", 2: "battery"}
STATUS = {0: "off", 1: "warming_up", 2: "starting", 3: "running",
          4: "defrost", 5: "charging", 6: "cooling", 7: "chillin"}
ERROR  = {0: "none", 1: "low_oil", 2: "high_engine_temp", 3: "low_battery",
          4: "ac_low_pressure", 5: "ac_high_pressure", 6: "starting_failure",
          7: "standby", 8: "engine_stalled", 9: "no_rpm", 10: "high_ac_pressure"}
SETVAL = {"off": 0, "climate": 1, "battery": 2}


def crc(d: bytes) -> bytes:
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return bytes([c & 0xFF, (c >> 8) & 0xFF])


def txn(ser, addr, fc, body=b""):
    req = bytes([addr, fc]) + body
    req += crc(req)
    ser.reset_input_buffer(); ser.write(req); ser.flush()
    h = ser.read(2)
    if len(h) < 2:
        raise RuntimeError("timeout")
    if h[1] & 0x80:      rest = ser.read(3)
    elif h[1] in (3, 4): n = ser.read(1); rest = n + ser.read((n[0] if n else 0) + 2)
    elif h[1] == 6:      rest = ser.read(6)
    else:                rest = ser.read(64)
    f = h + rest
    if len(f) < 4 or crc(f[:-2]) != f[-2:]:
        raise RuntimeError(f"crc/short {f.hex()}")
    if h[1] & 0x80:
        raise RuntimeError(f"exc 0x{f[2]:02X}")
    return f[2:-2]


def rd(ser, addr, wire):
    p = txn(ser, addr, 3, bytes([wire >> 8, wire & 0xFF, 0, 1]))
    return (p[1] << 8) | p[2]


def wr(ser, addr, wire, val):
    txn(ser, addr, 6, bytes([wire >> 8, wire & 0xFF, val >> 8, val & 0xFF]))


def snap(ser, addr):
    # mode(reg10/wire9), engine_status(reg22/21), control_status(reg23/22), error(reg17/16)
    return (rd(ser, addr, 9), rd(ser, addr, 21), rd(ser, addr, 22), rd(ser, addr, 16))


def main() -> int:
    ap = argparse.ArgumentParser(description="EF-G0B1R control-FSM Modbus monitor")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--addr", type=int, default=1)
    ap.add_argument("--secs", type=int, default=120)
    ap.add_argument("--set", choices=SETVAL, default="climate",
                    help="mode to command before watching (ACTUATES a real unit)")
    a = ap.parse_args()

    try:
        ser = serial.Serial(a.port, 9600, timeout=1.0)
    except serial.SerialException as e:
        print(f"FAIL: cannot open {a.port}: {e}")
        return 2

    with ser:
        wr(ser, a.addr, 9, SETVAL[a.set])
        print(f"[t=   0.0] wrote reg10 = {SETVAL[a.set]} ({a.set})")
        t0 = time.time(); last = None
        while time.time() - t0 < a.secs:
            try:
                cur = snap(ser, a.addr)
                if cur != last:
                    m, e, c, err = cur
                    print(f"[t={time.time()-t0:6.1f}] mode={MODE.get(m, m):8} "
                          f"engine={STATUS.get(e, e):10} control={STATUS.get(c, c):10} "
                          f"error={ERROR.get(err, err)}")
                    last = cur
            except Exception as ex:
                print(f"[t={time.time()-t0:6.1f}] ERR {ex}")
            time.sleep(2)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
