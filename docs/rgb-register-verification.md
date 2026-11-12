# RGB register verification

Firmware 1.2.3 extends the published 1.1.99 bench behavior with a mode rewrite
on every apply and explicit single-byte reads of 0x02 and 0x01 after completed
applies and refreshes. A mismatch or read failure clears APPLY_OK and uses
the existing bounded retry path. All-zero RGB shuts down before rewriting
mode. Nonzero channels retain the 1.1.99 enable mask.

The previously separate timing candidate is now included. The bus remains
100 kHz / 20 ms. Idle refresh is one second and the retry cap is one second.
The first retry stays at 500 ms. Fresh commands bypass older backoff and
receive an immediate attempt at the next superloop pass.

Firmware registers 84-91 are read-only diagnostics. Modbus wire addresses
are one less. Counters saturate at 65535 and reset at rgb_init / MCU boot.

| Firmware register | Meaning |
|---|---|
| 84 | Mode readback mismatches against 0x00 |
| 85 | Control readback mismatches against requested channel mask/shutdown |
| 86 | Failed readback checks due to transport errors |
| 87 | Last mode from a completed readback pair |
| 88 | Last control from a completed readback pair |
| 89 | Expected control for the most recent attempted check |
| 90 | Completed readback pairs, including mismatches |
| 91 | Most recent mismatched pair, mode in high byte, control in low byte |

A failure before the verification stage does not increment these readback
counters. Shared I2C counters at 80-83 remain separate. The last-mismatch
register persists after a later successful correction.

1.2.1 used a two-byte read starting at 0x01 and recorded a repeated second
byte after wake. 1.2.2 explicitly addresses both registers. The datasheet
describes auto-increment for writes and illustrates a single-byte read.
The initial paired-read results require corroboration by explicitly addressed
reads. On the bench, 1.2.2 explicitly addressed 0x02 and also read 0x35 after
yellow wake. Later verification read 0x00 after the normal retry path.
Both methods are retained. This supports a mode-state problem but does not
identify the electrical or internal cause.

https://www.lumissil.com/assets/pdf/core/IS31FL3293_DS.pdf

