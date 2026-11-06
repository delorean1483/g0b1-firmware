"""Lamp-only reproduction; only writes Modbus registers 76-78."""
import argparse
import json
import subprocess
import sys
import time

sys.path.insert(0, '/data/bench')
from bl_flash import Link
from rgb_bench import read_state, write_color

ap = argparse.ArgumentParser()
ap.add_argument('phase', choices=['dark', 'lit'])
ap.add_argument('--duration', type=float, default=60)
args = ap.parse_args()

def agent_state():
    return subprocess.run(['systemctl', 'is-active', 'gobi-agent'], capture_output=True, text=True).stdout.strip()

def emit(kind, **data):
    print(json.dumps({'kind': kind, 'elapsed_s': round(time.monotonic()-start, 3), **data}), flush=True)

def telemetry():
    version = link.read_holding(1, 1)[0]
    run = link.read_holding(52, 1)[0]
    errors = link.read_holding(66, 1)[0]
    recovery = link.read_holding(79, 4)
    applied, status = read_state(link)
    if version != 10191 or run != 0 or agent_state() != 'inactive':
        raise RuntimeError(f'Guard failed: version={version}, heater_run={run}, agent={agent_state()}')
    return {'version': version, 'heater_run_request': run, 'heater_transport_errors': errors,
            'applied': applied, 'lamp_status': status, 'recovery_attempts': recovery[0],
            'recovery_clears': recovery[1], 'recovery_failures': recovery[2],
            'last_recovery_lines': recovery[3]}

if agent_state() != 'inactive':
    raise RuntimeError('gobi-agent must remain inactive')
link = Link('/dev/ecofleet-rs485')
start = time.monotonic()
count = bad = 0
rc = 0
try:
    emit('before', phase=args.phase, **telemetry())
    write_color(link, (0, 0, 0))
    time.sleep(1)
    emit('dark_start', **telemetry())
    run_start = time.monotonic()
    next_sample = run_start
    while time.monotonic() - run_start < args.duration:
        v = (count * 37) % 256
        rgb = (0, 0, 0) if args.phase == 'dark' else (v, (count * 11) % 64, 255-v)
        write_color(link, rgb)
        count += 1
        time.sleep(0.1)
        applied, status = read_state(link)
        if status & 3 != 3 or applied != rgb:
            bad += 1
            emit('bad', write=count, requested=rgb, applied=applied, lamp_status=status)
        if time.monotonic() >= next_sample:
            emit('sample', writes=count, bad=bad, **telemetry())
            next_sample = time.monotonic() + 2
        if bad >= 5:
            rc = 2
            break
    emit('summary', phase=args.phase, writes=count, bad=bad,
         traffic_duration_s=round(time.monotonic()-run_start, 3), **telemetry())
except Exception as exc:
    rc = 3
    emit('exception', message=str(exc), writes=count, bad=bad)
finally:
    try:
        write_color(link, (0, 0, 0))
        time.sleep(0.5)
        emit('final_off_request', **telemetry())
    except Exception as exc:
        emit('cleanup_exception', message=str(exc))
        rc = max(rc, 3)
    link.close()
sys.exit(rc)
