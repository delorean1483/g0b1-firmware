# 1.2.6 pre-call / first-error transfer logging

Adds observation only to 1.2.5: existing timing, mode checks, recovery policy,
odd/even probe, stale ErrorCode clearing and heater COMMAND no-replay remain.
No printing, serial I/O, delays, recovery, or new error handling occurs in hooks.
Vendor HAL source is deliberately patched; preserve those hooks on regeneration.

The existing ten-word ring at registers 160–479 is unchanged. Its matching
32-word extension occupies registers 480–1503, same slot/sequence. Slots are
`(uint16_t)(sequence-1) % 32`. All register numbers are human-facing, wire = reg-1.
Read extension in chunks <=125 words and validate sequence + schema before use.
No additional ring entry is consumed for an extension.

| Extension word | Meaning |
|---|---|
| 0 | Sequence, must match legacy event |
| 1 | Schema = 1 |
| 2,3 | Pre-call ISR low/high |
| 4,5 | Pre-call CR2 low/high |
| 6 | Pre-call GPIO: SCL bit0, SDA bit1 |
| 7,8 | Pre-call HAL tick ms low/high |
| 9 | First-error software stage; bit15 means POST-RETURN fallback |
| 10,11 | First-error raw ISR low/high |
| 12,13 | First-error CR2 low/high |
| 14 | First-error GPIO |
| 15,16 | First-error HAL tick ms low/high |
| 17 | First-error HAL XferCount |
| 18 | First-error HAL XferSize |
| 19 | First-error pBuffPtr offset from current buffer |
| 20 | Current call's requested data length |
| 21 | First reason: 0 hardware bits, 1 software timeout, 2 post-return fallback, 3 none |
| 22,23 | First-error CR1 low/high (filter/control configuration) |
| 24 | Pre-call HAL State |
| 25 | First-error HAL State |
| 26,27 | Software ErrorCode before per-call clearing, low/high |
| 28,29 | OR of ISR observations made by error hooks (not a second atomic snapshot) |
| 30 | Hook observations in bits14:0, bit15 = software timeout observed at any point |
| 31 | Last software stage on HAL return |

Pre-call is captured immediately before software ErrorCode is cleared and the
blocking HAL routine invoked. Prior ErrorCode is context, not a fresh failure.
Initial BUSY/entry failures occur before the HAL initializes current-call buffer
fields: words 17–19 are 65535 (invalid), rather than stale preceding-call values.

## Software stages

1 ENTRY: before HAL; 2 INITIAL_BUSY_WAIT; 3 ADDRESS_WRITE_WAIT (first TXIS);
4 REGISTER_QUEUED (waiting register completion/TCR/TC); 5 READ_START_FIRST_BYTE
(read address or first receive byte); 6 DATA_TX; 7 DATA_RX; 8 WAIT_END;
9 reserved for reload (current clients never exceed 54 data bytes).

Stages identify software location, not exact wire phase. WAIT_END is set when
the final data byte has been queued and the software enters STOPF wait: that byte
may still be on the wire. CR2 AUTOEND means automatic STOP is armed; it does NOT
prove STOP has started. CR2 STOP and ISR STOPF are retained independently.
Likewise READ_START_FIRST_BYTE does not distinguish address ACK from first-byte
reception. XferCount/pointer reflect queued/consumed bytes, not ACKed bytes or
an exact failing bit. Do not claim a STOP collision solely from stage 8.

## Capture ordering

I2C_IsErrorOccurred observes raw NACKF/ARLO/BERR/OVR/TIMEOUT/PECERR/ALERT at entry,
before NACK flag clearing, STOP handling, TXDR flush or CR2 reset. It observes
again after NACK processing in case another error appeared. Timeout paths invoke
the hook before modifying software state/unlocking; the NACK STOP-timeout path
also does so. The first snapshot remains immutable; subsequent hooks only update
the OR/count fields. GPIO/CR2/tick reads follow the passed ISR read and are not
simultaneous electrical samples. Raw GPIO levels can evolve during capture.

The first observation is the first time the polling code sees an error, not the
instant hardware raised it. If a HAL error returns without a hook, the fallback
is clearly marked stage bit15/reason2 and must not be called pre-cleanup data.
Only failed HAL operations populate the ring. Pre-state of successful calls is
not retained; a later failure can still reveal pending pre-call hardware bits.

## Validation / bench collection

Host tests inject a pre-existing flag, a first NACK in register stage, a later
timeout/ARLO, and cleanup that clears registers: the extension preserves pre-call
and first-error snapshots while keeping later observations separately. Another
test requires explicit fallback marking for fake HAL paths without hooks. Existing
transport/probe/command-guard and service suites run too. Source checks verify hook
ordering before destructive cleanup and the relevant timeout assignments.

The eight-block red/blue test and 50 ms minimum write interval are unchanged.
Fresh mode/control verification is still required before each actual probe write.
The running agent owns the serial connection; a debugger borrows it at safe
boundaries. Larger records increase readout pauses; save timing, ring overflow and
sequence mismatches instead of silently discarding lost data. No heater command
is issued. Bounded probe duration and abort guards remain in firmware.

HAL XferSize is raw/stale until data configuration, so word 18 is not a valid current chunk count in address/register stages. XferCount and the buffer pointer are initialized before those helpers. Ignore XferSize there; decoded records expose chunk_remaining_valid.
