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
| 79 | `led_status` | RO | bit 0 present, bit 1 last apply OK, bit 2 pending |

- FC16 write of 76–78 (wire 75–77) applies all three channels in one lamp update.
- A value above 255 returns illegal-value. Earlier channels in the same FC16 are already stored.

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

`tools/rgb_bench.py` is stdlib only and runs on the Variscite. Stop the agent first, start it again after.
The agent owns the serial port, and its restore logic would put the saved color back over the test colors.

```bash
systemctl stop gobi-agent
python3 tools/rgb_bench.py --port /dev/ttyUSB0 diag      # or: set R G B | read | ramp red|green|blue
systemctl start gobi-agent
```

Each step prints the applied color and the status bits read back from 76–79.

1. `diag`: off, red, green, blue, yellow, cyan, magenta, white, off. Proves channel mapping, polarity, and that 0 = off.
2. `ramp red`, `ramp green`, `ramp blue`: brightness rises monotonically; note low-level behavior near 0.
3. Scope the gate waveforms and check MOSFET temperature at white, full brightness.
4. Persistence, with the Cortex agent running: reset the G0B1, power cycle, unplug RS-485. The saved color comes back each time.
5. Rapid writes (e.g. a `set` loop) while the heater coprocessor is polled: no new heater transport errors.

## Limitations

- No G0B1-controlled hardware shutdown line (EXT_RGB_OFF is not on HDR2). If the G0B1 hangs or I²C fails, the lamp keeps its last color. Normal off is PWM 0.

## Board notes

- Tested on G0B1 R0 with the breakout on ST1 (PC3 = VCC_EN powers 3P3_VCC).
- On R1, PC3 is LEDS_OFF, which only switches the G0B1's own on-board LEDs and is unrelated to the lamp.
- G0B1 R2 will carry the breakout circuitry on board. Only the I²C backend (`cube/Core/Src/drv_rgb_i2c2.c`) and board init (`app_main.c`) should change.
