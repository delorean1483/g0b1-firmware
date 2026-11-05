"""Drag-like rapid LED writes with status readback after each one.
Run on the Variscite next to rgb_bench.py + bl_flash.py, gobi-agent stopped.
usage: python3 rgb_rapid.py [interval_s] [duration_s] [lit|dark]"""
import sys
import time

from bl_flash import BAUD, Link
from rgb_bench import read_state, write_color

interval = float(sys.argv[1]) if len(sys.argv) > 1 else 0.2
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
dark = len(sys.argv) > 3 and sys.argv[3] == "dark"

link = Link("/dev/ecofleet-rs485", BAUD)
t0 = time.time()
n = 0
bad = 0
try:
    while time.time() - t0 < duration:
        v = (n * 37) % 256                        # sweeping red brightness, like a slider drag
        rgb = (0, 0, 0) if dark else (v, (n * 11) % 64, 255 - v)
        write_color(link, rgb)
        n += 1
        time.sleep(interval)
        applied, st = read_state(link)
        if st & 0x3 != 0x3 or applied != rgb:
            bad += 1
            print(f"#{n:4d} +{time.time() - t0:5.2f}s wrote {rgb} applied {applied} st=0x{st:04X}", flush=True)
            if bad >= 5:
                break
finally:
    write_color(link, (0, 0, 0))
    time.sleep(0.5)
    applied, st = read_state(link)
    print(f"done: {n} writes in {time.time() - t0:.1f}s, {bad} bad; final applied {applied} st=0x{st:04X}", flush=True)
    link.close()
