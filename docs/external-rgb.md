# External RGB lamp (IS31FL3293) — wiring, registers, bench bring-up

The G0B1 drives an external RGB lamp through an IS31FL3293 LED driver (I²C2, address 0x6C).
Colors are Modbus holding registers; the Cortex agent saves and restores the color.

## Registers

Wire address = firmware register − 1 (so 75–78 on the wire). Slave 1, 9600 8N1, RS-485.

| Reg | Name | Access | Meaning |
|---|---|---|---|
| 76 | `led_red` | RW | 0–255. Write = requested, read = applied |
| 77 | `led_green` | RW | 0–255, same |
| 78 | `led_blue` | RW | 0–255, same |
| 79 | `led_status` | RO | bit 0 present (chip found and initialized; stays set after an apply failure until a re-init fails), bit 1 last apply OK, bit 2 pending |

- FC16 write of 76–78 (wire 75–77) applies all three channels in one lamp update.
- A value above 255 returns illegal-value. In an FC16 with a bad later value, the earlier channels are stored and applied on the next pass. FC06 single-register writes apply channel by channel.
- An apply writes only the PWM registers and the PWM latch. Do not rewrite the configuration on every apply: on the 2026-11-05 bench, configuration rewrites while the lamp was lit faulted the shared I2C2 bus about once per four rewrites (1 s refresh: +17 bus clears per lit minute, +1 per dark minute), while PWM-only applies were benign (4 clears in 1,372 lit writes).
- Idle refresh: every 10 s with nothing pending the firmware rewrites the chip configuration and the applied color (no reset, no flicker). A chip that lost power and came back dark is restored within 10 s.
- After a failed apply or refresh, bit 1 clears and the firmware retries with backoff (0.5 s up to 8 s). Each retry rewrites the configuration plus the requested color **without** the 0x3F reset, because a reset blanks a lit lamp, and re-lighting it from dark faulted the bus on the bench. Only after 3 consecutive failures does the next attempt re-initialize the chip with the reset.
- Each I2C transfer has a 10 ms HAL timeout, but a failing call can take about 25-35 ms in total (HAL BUSY/STOPF waits), so budget for that per failed attempt, not 10 ms.

## Wiring (G0B1 R0, breakout on ST1)

| R0 header ST1 | Breakout HDR2 | Signal |
|---|---|---|
| G1 | 1 | switched 3.3 V |
| G18 | 2 | I2C2_SDA |
| G13 | 3 | I2C2_SCL |
| B2 | 9 | GND |
| C4 | 10 | protected +12 V |

Lamp wires: HDR2 4 blue, 5 green, 6 red. The lamp's black (common anode) wire goes to +12 V.

Channel map: OUT1 green (Q1), OUT2 red (Q3), OUT3 blue (Q4). The driver sinks current to turn the lamp on, so a higher PWM value is brighter (no inversion expected).

## Bench procedure

`tools/rgb_bench.py` is stdlib only and runs on the Variscite. Copy both `tools/rgb_bench.py` and `tools/bl_flash.py` to the same directory on the unit (the bench script imports `bl_flash`).
Stop gobi-agent first and start it again after. The agent owns the serial port, and its restore logic would put the saved color back over the test colors.

```bash
systemctl stop gobi-agent
python3 rgb_bench.py --port /dev/ecofleet-rs485 diag      # or: set R G B | read | ramp red|green|blue
systemctl start gobi-agent
```

The Variscite's RS-485 port is `/dev/ecofleet-rs485`. Use `/dev/ttyUSB0` only as an alternative, e.g. on a USB RS-485 adapter.

Each step prints the applied color and the status bits read back from 76–79.

1. `diag`: off, red, green, blue, yellow, cyan, magenta, white, off. Proves channel mapping, polarity, and that 0 = off.
2. `ramp red`, `ramp green`, `ramp blue`: brightness rises monotonically; note low-level behavior near 0.
3. Scope the gate waveforms and check MOSFET temperature at white, full brightness.
4. Persistence (needs the Plan 2 Cortex build, which saves and restores the color; with today's agent the saved color does not come back, and that is not a firmware failure): with that agent running, reset the G0B1, power cycle, unplug RS-485. The saved color comes back each time.
   Without the Plan 2 agent, check only that after a G0B1 reset the lamp is dark with bits 0 and 1 set, then a new `set` lights it.
5. Rapid writes while the heater coprocessor is polled: no new heater transport errors. A `set` loop runs at only about 2 Hz, so it is not a rapid-write test. Do this step by dragging on the Cortex color page (needs the Plan 2 Cortex build).
6. Chip power glitch: unplug and replug the breakout (or its 3.3 V) while a color is set. The lamp is restored within about 10 s by the idle refresh.

## Limitations

- No G0B1-controlled hardware shutdown line (EXT_RGB_OFF is not on HDR2). If the G0B1 hangs or I²C fails, the lamp keeps its last color. Normal off is PWM 0.

## Board notes

- Tested on G0B1 R0 with the breakout on ST1 (PC3 = VCC_EN powers 3P3_VCC).
- On R1, PC3 is LEDS_OFF, which only switches the G0B1's own on-board LEDs and is unrelated to the lamp.
- G0B1 R2 will carry the breakout circuitry on board. Only the I²C backend (`cube/Core/Src/drv_rgb_i2c2.c`) and board init (`app_main.c`) should change.

### Bench shutdown caveat

The IS31FL3293 Rev. B datasheet says an SDB-low hardware shutdown retains registers; its rising edge resets only the I2C module. A power-on or software reset clears registers. These are distinct faults. The reported floating Q2 gate must be verified/reworked separately; firmware cannot hold SDB high while Q2 pulls it low. A blue lamp is not a programmed fallback.
