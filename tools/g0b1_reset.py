"""Reset the G0B1 via Modbus reg 34 (wire 33) and watch the LED + heater status.
Run on the Variscite from the directory holding bl_flash.py, gobi-agent stopped."""
import sys
import time

from bl_flash import SLAVE_ADDR, Link

PORT = "/dev/ecofleet-rs485"


def snap(link, label):
    ver = link.read_holding(1, 1)[0]                 # reg 2
    r, g, b, st = link.read_holding(75, 4)           # regs 76-79
    hp = link.read_holding(67, 7)                    # regs 68-74 heater telemetry
    print(f"{label:>8}: ver={ver} led=({r},{g},{b}) st=0x{st:04X} heater68-74={hp}", flush=True)


link = Link(PORT)
snap(link, "before")
link._send(bytes([SLAVE_ADDR, 0x06, 0x00, 33, 0x00, 0x01]))  # reg 34 write -> NVIC_SystemReset
link.ser.timeout = 0.5
print("reset sent, reply bytes:", link.ser.read(8).hex() or "none", flush=True)
link.ser.timeout = 2.0
t0 = time.time()
while time.time() - t0 < 20:
    time.sleep(2)
    try:
        snap(link, f"+{time.time() - t0:4.1f}s")
    except Exception as e:  # app still booting / bootloader window
        print(f"+{time.time() - t0:4.1f}s: {type(e).__name__}: {e}", flush=True)
link.close()
sys.exit(0)
