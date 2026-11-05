"""Poll the L011 heater register file (s_live) over SWD hot-plug and print one line per read.
usage: STM32_PROG=/path/to/STM32_Programmer_CLI python3 l011_watch.py <count> <interval_s>
Addresses are for the L011 build from g0b1-firmware main 5d65728 (check with arm-none-eabi-nm after a rebuild)."""
import os
import re
import subprocess
import sys
import time

CLI = os.environ.get("STM32_PROG", "STM32_Programmer_CLI")   # path to STM32CubeProgrammer's CLI
S_LIVE = 0x20000369
PH = {0: "off", 1: "detect", 2: "start", 3: "run", 4: "stopreq", 5: "cool", 6: "fault"}
TY = {0xFF: "auto", 0: "none", 1: "VEVOR", 2: "ATERM"}
FL = ["pres", "fresh", "safe", "commsF", "ifacePwr", "hostTO", "echoF", "detDone", "faultL"]


def read_map():
    out = subprocess.run([CLI, "-c", "port=SWD", "mode=HOTPLUG", "-r8", hex(S_LIVE), "0x46"],
                         capture_output=True, text=True).stdout
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)
    b = []
    for line in out.splitlines():
        if line.startswith("0x") and ":" in line:
            b += [int(x, 16) for x in line.split(":", 1)[1].split()]
    return b[:0x46]


count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
for _ in range(count):
    b = read_map()
    if len(b) < 0x46:
        print("read failed")
        time.sleep(interval)
        continue
    u16 = lambda r: b[r] | (b[r + 1] << 8)
    fl = u16(0x22)
    print(f"{time.strftime('%T')} type={TY.get(b[0x10], b[0x10])} act={TY.get(b[0x21], b[0x21])} "
          f"ph={PH.get(b[0x20], b[0x20])} flags={'|'.join(n for i, n in enumerate(FL) if fl >> i & 1) or '-'} "
          f"vendor={b[0x26]}.{b[0x27]} fault={b[0x28]} supply={u16(0x2A)}mV fan={u16(0x2C)} "
          f"valid={u16(0x38)} crc={u16(0x3A)} xport={u16(0x3C)} echo={u16(0x3E)} age={u16(0x36)} "
          f"ls=0x{b[0x44]:02X} det={b[0x45]}", flush=True)
    time.sleep(interval)
