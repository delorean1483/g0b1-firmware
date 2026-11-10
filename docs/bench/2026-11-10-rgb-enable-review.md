# IS31 blue-output bench review

This branch contains the bench-tested 1.1.99 source based on PR #17 at
d1cdef3c5c0950f5178e50985521e30d11703133. It includes the 1.1.97 shared-bus
recovery experiment, 1.1.98 shutdown behavior, and 1.1.99 output-enable mask.
The local one-second-refresh 1.2.0 candidate is separate.

## Changes

- Recover after three consecutive qualifying shared-transport failures.
  Success and pure AF with idle lines and BUSY clear reset the streak.
  The streak is shared across clients. Idle NACKs can include data NACKs.
- On idle lines, use the F1-style open-drain SDA-low, SCL-low, SCL-high,
  SDA-high sequence before resetting the G0 I2C peripheral. This is an
  experimental adaptation, not identification of the F1 silicon defect.
- Preserve the one-second shared recovery cooldown and heater COMMAND
  at-most-once handling inherited from PR #17.
- Leave the IS31 shut down after initialization. RGB zero applies/retries
  and idle refreshes keep it shut down.
- Wake with a configuration restore, PWM and latch before output enable.
- Disable every output whose requested PWM is zero. Yellow uses 0x35.
  Lit updates change enables after the new PWM latch when the mask changes.
- Preserve 100 kHz / 20 ms, 10-second idle refresh, and 8-second retry cap.

## Scope findings

CH1=SCL, CH2=SDA, CH3=the user-connected blue-output node. All probes 10x.
The exact CH3 physical node was not independently confirmed. Electrical
low correlated with blue drive. There was no optical sensor. Long records
were sampled at 1 MHz, sufficient for transactions but not narrow analog
glitches. Times in the attached decodes are relative to each acquisition.

In the failing 1.1.97 trial, the initial zero-PWM data byte NACKed. About
3.5 seconds later the service reset and initialized the IS31. The trace
decodes ACKed 0x02=0x00, zero PWM, and latch. Blue became active at the
subsequent 0x01=0x75 write. A further ACKed zero-PWM/latch did not clear it.
Blue stayed active for 9.997 seconds and cleared during the next refresh's
first 0x02=0x00 write, before PWM and latch. Agent commands contained yellow,
dim yellow, and zero, with no blue command.

In 1.1.98, zero requests used 0x01=0x70 and stopped blue immediately.
Waking yellow still drove blue at 0x01=0x75 despite zero-blue PWM and latch.

In 1.1.99, eight valid yellow-on/dim/off trials showed no sustained blue
drive. The final raw windows decode ACKed yellow PWM, latch, and 0x01=0x35.
CH3 stayed inactive through wake, zero/shutdown, and the ten-second refresh.
An initial trial was excluded because the agent restored a saved color
containing blue after restart. The eight trials began from verified off.
This prevents the measured failure for this test set. It does not prove
the original electrical fault or the IS31 mode corruption mechanism solved.

## Sung's mode-register hypothesis

0x02 bits 5:4 select OUT3 mode. Current Level Mode ignores PWM and behaves
as PWM=4096. This fits blue surviving PWM/latch and clearing at 0x02=0.
Both 0x01 and 0x02 are documented R/W. No live register readback was added
in this branch. The captured 0x02=0 after reset means corruption is still
a hypothesis. The later 0x01 byte could have been accepted at the wrong
internal pointer, or another cause could exist. 1 MHz sampling does not
exclude narrower glitches seen differently by the target input.

References
- https://www.lumissil.com/assets/pdf/core/IS31FL3293_DS.pdf
- PR #17 https://github.com/delorean1483/g0b1-firmware/pull/17

## Validation and reproducibility

40 targeted host tests passed before the 1.1.99 flash. The review snapshot
has been rebuilt independently to verify the original slot image hashes.

Slot A SHA256
B83E8E9D4624D2F233018600A1C789D427A3037003D6B8E9AE4C620657D82A4C

Slot B SHA256
58A6AA25C937724C321A7FAA75375E5FED9E811740D36DA7C19A4BCD85571EC2

Each image is 45,460 bytes. The existing RWX linker warning remains.
The bench runs slot A 1.1.99, with slot B 1.1.98 for rollback.
gobi-agent is running, lamp off, heater off. No live change was made
while preparing this review branch.

The attached text decodes preserve the failing 1.1.97 event, 1.1.98 wake
behavior, and passing 1.1.99 windows. Original raw windows and metadata
are saved locally under outputs/yellow-off-1.1.97-windowed,
outputs/rgb-shutdown-1.1.98/yellow-wake-tests, and
outputs/rgb-mask-1.1.99/yellow-tests.
