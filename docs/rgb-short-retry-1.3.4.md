# G0B1 1.3.4 short lamp retry

On the 50 ms idle-bus retry, a lit RGB request that preserves the enabled channels writes the six PWM bytes at 0x19, latches via 0x28, and explicitly reads mode 0x02 and control 0x01. That is four transactions on the successful short path. A confirmed mode/control mismatch escalates to the existing full restore. A failed PWM write or unverified short retry uses normal backoff. Wake and channel-mask changes use the existing configuration path.

Each verification read that fails gets one additional read of the same register. When a successful write/latch is followed by persistently unreadable verification, the next eligible pass reads only. A mismatch on that pass can restore the full configuration. A second unresolved read-only pass returns to normal restore backoff. The one-second shared I2C recovery cooldown and heater command guard still apply.

The 1.3.4 images were built without RGB_BENCH_INJECTION. PWM remains 400 Hz, with the same one-second full refresh as 1.3.3. The retry ring records the start and result in this build. The soak uses the same red/green requests and CH3 method as the 1.3.3 comparison. A temporary read-only agent collects normal Modbus snapshots.
