# Shared I2C2 recovery (G0B1 firmware 1.1.91+, release candidate 1.1.95)

Both the heater client at 0x64 and RGB client at 0x6C use `drv_i2c2_bus.c`.
I2C2 runs at 100 kHz standard mode (0x60302730) with a 20 ms whole-transfer timeout for both clients. The 1.1.91 experiment and the matched 1.1.93 build ran at 50 kHz (0xF0102527). The matched comparison against 1.1.94 at 100 kHz showed no difference (130 vs 129 clears, 167 vs 165 heater transport errors), so 100 kHz is kept.

On a transport fault the driver disables I2C2, releases PA11/SCL and PB14/SDA,
and clears a slave holding SDA with at most nine open-drain clocks and a STOP.
Each wait for an externally held SCL is bounded to 5 ms. All high outputs are
open-drain releases with external pull-ups. It always restores GPIO alternate
functions, resets only I2C2's peripheral registers, initializes the HAL handle,
and restores timing and analog/digital filters. Recovery is globally rate-limited to once per 1000 ms after the previous recovery completes, across both clients. During the cooldown the HAL is skipped only while a line is still held or the last clear failed. A failed retry, or a new fault on an idle bus, just returns failure with no second clear, so the other client keeps working. Idle NACKs, including a retry NACK, do not block the other client. There is one retry for reads and repeatable writes;
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
