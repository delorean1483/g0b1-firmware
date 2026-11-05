#!/usr/bin/env bash
# I2C bus recovery driven from the L011 over SWD (hot-plug, core keeps running):
# I2C1 off, PA9 (SCL) / PA10 (SDA) as open-drain GPIO, up to 9 SCL pulses until
# SDA reads high, then a STOP. Restores the L011 with a reset at the end.
CLI="${STM32_PROG:-STM32_Programmer_CLI}"   # path to STM32CubeProgrammer's CLI
GPIOA=0x50000000; MODER=0x50000000; IDR=0x50000010; BSRR=0x50000018; I2C1_CR1=0x40005400

swd() { "$CLI" -c port=SWD mode=HOTPLUG "$@" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep -E "^0x|rror"; }
idr() { swd -r32 $IDR 4 | awk '{print $3}'; }
lines() { local v=$((16#$(idr))); echo "SCL=$(( (v >> 9) & 1 )) SDA=$(( (v >> 10) & 1 ))"; }
step() { swd -w32 $BSRR $1 >/dev/null; echo "$(date +%T) $2 -> $(lines)"; }

echo "start            -> $(lines)"
swd -w32 $I2C1_CR1 0x000000BE >/dev/null                 # PE=0: I2C1 releases the pins
swd -w32 $BSRR 0x00000600 >/dev/null                     # ODR9/10 = 1 (released) before switching mode
m=$((16#$(swd -r32 $MODER 4 | awk '{print $3}')))
m=$(( (m & ~(0xF << 18)) | (0x5 << 18) ))                # PA9, PA10 -> general-purpose output (open-drain)
swd -w32 $MODER $(printf '0x%08X' $m) >/dev/null
echo "pins as GPIO     -> $(lines)  MODER=$(printf '0x%08X' $m)"

for i in 1 2 3 4 5 6 7 8 9; do
  step 0x02000000 "clk $i SCL low "
  out=$(step 0x00000200 "clk $i SCL high")
  echo "$out"
  [[ "$out" == *"SDA=1"* ]] && { echo "SDA released after clock $i"; break; }
done

step 0x02000000 "STOP: SCL low   "
step 0x04000000 "STOP: SDA low   "
step 0x00000200 "STOP: SCL high  "
step 0x00000400 "STOP: SDA high  "
"$CLI" -c port=SWD mode=HOTPLUG -rst 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep -E "Software reset is"
echo "L011 reset       -> $(lines)  CR1=$(swd -r32 $I2C1_CR1 4 | awk '{print $3}')"
