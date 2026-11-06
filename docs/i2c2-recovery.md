# Shared I2C2 recovery (bench firmware 1.1.91 and review builds)

Both the heater client at 0x64 and RGB client at 0x6C use `drv_i2c2_bus.c`.
The original 1.1.91 experiment used 50 kHz timing (0xF0102527) and 20 ms timeouts. Matched review builds are 1.1.93 at 50 kHz/20 ms and 1.1.94 at 100 kHz/10 ms (0x60302730). Define G0B1_BENCH_I2C2_100KHZ=1 for the latter; both include identical recovery and lamp logic.

On a transport fault the driver disables I2C2, releases PA11/SCL and PB14/SDA,
and clears a slave holding SDA with at most nine open-drain clocks and a STOP.
Each wait for an externally held SCL is bounded to 5 ms. All high outputs are
open-drain releases with external pull-ups. It always restores GPIO alternate
functions, resets only I2C2's peripheral registers, initializes the HAL handle,
and restores timing and analog/digital filters. Recovery is globally rate-limited to once per 1000 ms after the previous recovery completes, across both clients. A held line or failed clear suppresses HAL calls during that cooldown; healthy transfers still proceed. Idle NACKs, including a retry NACK, do not block the other client. There is one retry for reads and repeatable writes;
permanent shorts return failure to the existing client backoff instead of
looping or resetting either microcontroller. Normal address NACKs on an idle bus
do not trigger recovery.

Only the superloop may call these blocking functions; both clients currently do.
Write retries repeat the same register payload. No heater-control semantics,
L011 firmware, I2C1 configuration, services or persisted heater type are changed.

Read-only Modbus registers (firmware numbering; wire address = register minus 1):

| Register | Meaning |
|---|---|
| 80 | Recovery attempts |
| 81 | Successful bus clears (not necessarily successful subsequent transfers) |
| 82 | Failed bus clears/reinitializations |
| 83 | Line levels before the most recent recovery: bit 0 SCL, bit 1 SDA |

The counters saturate at 65535 and reset on an MCU reset. A last-line value of
1 means SCL high / SDA low; 2 means SCL low / SDA high. These are sampled GPIO
levels, not waveform timing or proof of which device held a line.

The host test compiles the actual recovery driver and both client adapters with
fake HAL/GPIO functions. It covers healthy traffic, optional-device NACK,
master-held SCL, SDA clear clocks/STOP, permanent line faults, bounded retry,
initialization/filter failure, timing/filter preservation, client addressing,
timeouts and read-only diagnostics. See `Tests/test_drv_i2c2_bus.c`.

The separately recorded bench report and oscilloscope capture distinguish
automatic recovery from elimination of the underlying turn-on disturbance.
