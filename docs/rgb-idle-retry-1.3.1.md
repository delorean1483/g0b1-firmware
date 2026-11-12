# G0B1 1.3.1 idle-bus lamp retry

After a lamp write fails and the immediate full restore plus readback cannot verify the request, the RGB service can schedule one retry 50 ms after the failure handling completes. It samples the physical SCL/SDA levels at scheduling and again when due. If either line is held at the deadline, it skips the fast transfer and uses the normal backoff. The shared I2C2 transport still handles cooldown, recovery and guarded heater command writes. The allowance resets when the RGB service applies or refreshes successfully. A fresh RGB command supersedes a queued retry.

Register 153 holds the retry ring sequence. Registers 1888–2015 contain 16 rows of 8 words. The slot index is (sequence - 1) modulo 16.

| Word | Field |
|---|---|
| 0 | Sequence |
| 1–2 | MCU milliseconds, low then high |
| 3–5 | Requested R, G, B |
| 6 | Low byte action, high byte GPIO snapshot (SCL bit 0, SDA bit 1) |
| 7 | Failure bits for the corresponding attempt |

Actions are 1 queued, 2 verified, 3 failed, 4 line held at deadline, and 5 superseded. Word 7 bits 0, 1 and 2 denote failed initial write, failed immediate restore and failed readback. The existing I2C transfer-failure ring records failed register, HAL error and line levels on the same MCU clock. The RGB restore/enable ring and registers 139–140 supply restore context.
