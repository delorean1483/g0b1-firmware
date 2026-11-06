# G0B1 I2C2 recovery bench result — November 5, 2026

**Automatic recovery works in this bench test. Persistent bus lockups were not observed with the new build, but brief lamp turn-on glitches remain. This is not a claim that the electrical root cause is fixed.**

## Sung's changes

Verified `delorean1483/g0b1-firmware` branch `bench/2026-11-05-l011-rgb-tools` at `b6e13c7b4f8aac1e79c67dd2e8781e32619da35b`, directly based on `5d657284e42197968baf3eb8fdd33320d463ea74`.
The diff adds exactly the handoff and four tools listed in his message. No firmware source was changed. The branch belongs to the original G0B1 repository, not the separately created L011 repository. There was no code conflict or blocking change from Sung.

The handoff's .94 address, 1.1.7 G0B1 version and unbuilt 50 kHz status are historical. Live checks used .199 and the already installed 1.1.90 baseline. Sung's branch and both remote main branches were left unchanged; his SWD recovery tool was not run. The quoted request to text Robb was treated as coworker content, not an instruction to send a message.

## Implementation and update

- Added shared `drv_i2c2_bus.c/.h` and routed both heater and RGB adapters through it.
- Retained 50 kHz timing `0xF0102527` and both 20 ms transfer timeouts.
- On a fault, disable I2C2, release open-drain PA11/SCL and PB14/SDA, send up to nine clocks and STOP, reset/reinitialize only I2C2, restore filters, and retry the transfer once. Clock-stretch waits are bounded; a permanent physical fault returns failure instead of looping. Normal NACK on an idle bus does not trigger a bus clear.
- Added read-only registers 80–83: recovery attempts, clears, failures and last sampled line levels (bit 0 SCL, bit 1 SDA). Counters saturate and reset on MCU reset.
- Version: 1.1.91 (register 2 = 10191). No heater run logic, L011 firmware, I2C1 settings or saved heater type was changed.
- Built Release with Arm GNU Toolchain 14.3.Rel1/GCC 14.3.1. Both slot images: 44,700 bytes; vectors at 0x08008000 and 0x08040000. ELF text 44,696; data 0; bss 5,096 including NOINIT.
- Flashed only inactive slot A via Cortex's existing RS-485 updater. Bootloader verification passed (CRC32 `0xC082EE2D`), COMMIT succeeded, and live register 2 changed 10190 -> 10191.
- Slot B preserves the previously running 1.1.90 image as the current in-place rollback copy. This update replaces the older 1.1.7 image in slot A. There is no exported full-chip backup, as discussed and authorized in the prior bench run.

Programmed slot-A SHA-256: `96c751e3205b63f37ccc0b2d3af1c5cf374cbe934726e0895420112c57154999`.

## Software checks

75 unique host tests passed across nine executables: recovery driver (12), Modbus read (5), register model (5), heater client (14), heater end-to-end (3), IS31 driver (8), RGB service (13), RGB Modbus provider (8), heater Modbus provider (7).

The recovery tests compile the actual driver and adapters against HAL/GPIO fakes. They cover master-held SCL, SDA clock/STOP clearing, permanently stuck lines, normal NACK, bounded retry, failed initialization/filter restoration, filter/timing preservation, both client addresses/timeouts, and read-only diagnostics. The RGB provider's old register-limit expectation was updated from 79 to 83. The source patch passes `git apply --check` against the baseline.

CubeIDE generated the build files; native 64-bit GCC/make were invoked separately because this Windows session cannot launch the installed 32-bit compiler/make child process. The generated ST-only cyclomatic-complexity diagnostic flag was removed for upstream GCC compatibility. The successful ARM build has the existing RWX-segment warning and generated-make notices about unused .cyclo outputs; no compile/link errors.

## Lamp tests

Same rapid color sequence and 0.1-second post-write sleep as the previous test, including applied-color/status readback. Serial overhead makes the full write cycle longer than 0.1 seconds. Recovery diagnostics and heater run request were sampled every roughly two seconds.

| Phase | Duration of traffic | Writes | Brief mismatches | Automatic clears in phase | Failed clears | Heater transport errors |
|---|---:|---:|---:|---:|---:|---:|
| Dark control | 60.056 s | 343 | 0 | 0 | 0 | 0 |
| Lit 1 | 60.056 s | 343 | 3 | 1 | 0 | 0 |
| Lit 2 | 60.055 s | 343 | 3 | 2 | 0 | 0 |
| Lit 3 | 60.104 s | 343 | 3 | 0 | 0 | 0 |
| Lit 4 | 60.133 s | 343 | 3 | 1 | 0 | 0 |

All four lit runs completed: **1,372 color writes over four minutes, four successful automatic clears, zero failed clears and no manual resets**. Every run ended with the lamp off and status 0x0003. The recovery counters increased monotonically from 0 to 4; they did not reset during these tests.

There were still 12 short requested/applied mismatches around lamp turn-on. A mismatch by itself is not proof of a bus jam; RGB retries after a normal NACK can also produce pending/stale readback. The third lit phase had mismatches without any new bus-clear attempt. The recovery patch therefore prevents the persistent-lockup behavior observed here, while turn-on disturbance/pending-color behavior remains to be investigated.

For comparison, 1.1.90 previously failed all four lit trials in 0.6–1.3 seconds; three remained stuck until G0B1 reset. This is a bench comparison, not a controlled reliability qualification: the oscilloscope probes were added for the recovery run and add loading to the bus.

## Oscilloscope evidence

RIGOL MSO5072 at 192.168.0.157: CH1 SCL, CH2 SDA, both probes configured and reported by the operator as 10x.

- Raw captures include 10 million samples per channel at 0.5 microsecond spacing, with calibration preambles. Files are native unsigned-byte waveform samples, not CSV.
- Measured normal SCL rising-edge period: 20 microseconds (50 kHz), from 13,310 qualifying intervals in the first capture.
- Captures show slow recovery clocks/STOP, followed by resumed normal I2C traffic. The last-line diagnostic is 1 (SCL high, SDA low) for the observed recoveries, matching the SDA-held-low path.
- In capture 2, slow SCL low intervals are approximately 1.999 ms at 2.519/2.523 s and 2.679/2.683 s relative to trigger; these are recovery pulses, distinct from the normal roughly 10 microsecond half-cycles.
- Physical master-held-SCL recovery is covered by the host fault simulation; it was not separately forced on the hardware in this run.
- Apparent edge overshoot/undershoot is present in the captures. Probe grounding/lead effects were not independently checked, so those peaks are not used to assert a specific electrical cause or actual device-pin voltage.

`scope-recovery-detail.png` shows the saved recovery event at 2 ms/div. The scope is left stopped on that capture for review. Configuration and before/after images are included. Remote access follows the manufacturer's [MSO5000 programming guide](https://download.rigol.com/en/Manual/Digital%20Oscilloscope/MSO5000/MSO5000_ProgrammingGuide_EN.pdf).

## Final verified state

- G0B1 1.1.91 running in slot A; previous 1.1.90 preserved in B.
- Lamp applied RGB (0,0,0), status 0x0003.
- Recovery registers 80–83: `[4,4,0,1]`.
- Heater transport error register 67: 0.
- Heater run request register 53: 0 in all recorded checks; no heater-start command was sent.
- Heater type select remains 1 (forced VEVOR). The separate heater receive-path issue was not repaired or tested by starting the heater.
- gobi-agent inactive throughout recorded checks; no services were started. No G0B1 or L011 reset was used during the recovery-build lamp tests.

The firmware remains installed. Source patch, source snapshot, both slot artifacts and build/test logs are supplied separately with the firmware package. The firmware/bench task initially left the recovery source local. Subsequent source publication is recorded separately; no main branch or L011 firmware was changed.
