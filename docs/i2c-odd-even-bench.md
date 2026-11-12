# 1.2.5 I2C error logging and odd/even bench probe

Bench-only diagnostics, built on the 1.2.3 mode-readback/faster-refresh source.
1.2.5 clears HAL ErrorCode before each blocking call: HAL checks initial BUSY
before clearing ErrorCode itself, so a busy-wait timeout must not inherit the
previous slave's ARLO/NACK flags. Initial 1.2.4 per-device flags can contain this
carryover; its probe writes had fresh successful verification reads immediately
before them, but use the corrected 1.2.5 run for final attribution.
The existing 100 kHz clock, 20 ms transfer timeouts, three-failure recovery
threshold, one-second recovery cooldown and heater COMMAND no-replay guard remain.
The probe is dormant after boot and must be explicitly started. Heater commands
are not issued by the probe.

Register numbers below are human-facing; wire addresses are one less.

## Transport counters and failure events

92–98: IS31 (address 0x6c); 99–105: L011 (address 0x64).
Each seven-register block is HAL attempts, successes, ARLO, NACK/AF, timeout,
HAL_BUSY, other HAL_ERROR. 106/107 count pre-HAL cooldown deferrals for each
device. Counters saturate at 65535 and reset at boot. Error categories can overlap
if HAL reports more than one flag. Optional idle NACKs are logged even when they
are excluded from bus recovery.

135 is the rolling 16-bit event sequence. Registers 160–479 expose 32 slots of
ten words. Slot = (uint16_t)(sequence - 1) modulo 32. Each failed actual HAL
operation records:

| Word | Meaning |
|---|---|
| 0 | Sequence |
| 1,2 | HAL tick milliseconds, low/high |
| 3 | 7-bit slave address in high byte, register in low byte |
| 4 | HAL error flags: ARLO=2, AF/NACK=4, TIMEOUT=32 |
| 5 | HAL status in low nibble; write=0x10; retry=0x20; GPIO levels in bits 9:8 |
| 6,7 | ISR after HAL returns, low/high |
| 8 | Transfer length |
| 9 | Intended first/last write bytes, high/low; zero for reads |

GPIO levels: SCL=bit0, SDA=bit1. Sampled after HAL cleanup, before any recovery.
HAL preserves detected error flags in ErrorCode; ISR hardware flags may already
have been cleared by HAL. This is not a pre-cleanup raw-ISR or waveform capture.
Read ring slots promptly and validate each stored sequence; overwritten events
must be reported. Aggregate counters remain authoritative if the ring overruns.
Successful transfers and pre-HAL deferrals do not consume ring slots.

## Bounded odd/even probe

The experiment writes only IS31 register 0x04, with value 0x01 (odd) or 0x00
(even). Pattern hold is unused in PWM mode. Every write requires separate fresh
reads of mode 0x02 == 0 and control 0x01 == the RGB service's expected value.
These verification reads add I2C traffic and must be acknowledged when comparing
this experiment with previous steady-color tests.

| Register | Meaning |
|---|---|
| 108 RW | 0 abort; 1 start odd; 2 start even |
| 109 RW | Duration seconds, 1–120, default 60 |
| 110 RW | Minimum interval ms, 10–1000, default 50 |
| 111 | State: 0 idle, 1 active, 2 complete, 3 aborted |
| 112 | Data value: 1 odd / 0 even |
| 113 | Abort reason: 0 none, 1 user, 2 heater not off, 3 RGB request changed |
| 114,115 | Verified write calls / successful actual writes |
| 116–121 | Probe-write ARLO, NACK, timeout, BUSY, deferred, other |
| 122,123 | Successful mode/control verification pairs / verification skips |
| 124,125 | Mode/control mismatch skips |
| 126–128 | Expected control, last actual mode, last actual control |
| 129,130 | Elapsed milliseconds low/high |
| 131–133 | Recovery attempts, successful clears, failed clears since start |
| 134 | Actual HAL probe-write attempts, excluding deferrals |

Start requires heater mode/run off, applied RGB equal to requested RGB, and
RGB_APPLY_OK. The probe aborts if heater state or requested color changes.
Failed verification or a pending RGB service apply skips the probe write.
Mismatch requests repair through the normal RGB service, at most once per second;
it never tests register 0x04 in a known non-PWM mode. Probe writes are never
automatically replayed. Configuration changes while active are rejected.

At 50 ms spacing, duration 60 s targets approximately 1200 writes. No catch-up
burst follows delays, and faults/verification skips reduce the actual denominator.
Report failures per actual HAL attempt, along with all verification skips.

For the live bench run, gobi-agent remains active. A debugger borrows its existing
serial connection at transaction boundaries to start/poll the probe, then allows
all threads to run between samples. Record those brief pauses and detach afterward.
Do not open a competing serial client while the agent owns the port.
