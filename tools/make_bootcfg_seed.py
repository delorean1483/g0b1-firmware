#!/usr/bin/env python3
# make_bootcfg_seed.py — generate the one-time boot-config seed record for
# SWD provisioning (see tools/provision_boot.md §3, Option A).
#
# Produces a 48-byte bootcfg_t record marking Slot A COMMITTED + active, with
# slot A's exact length + CRC-32/IEEE-802.3 (zlib) filled in from the flashed
# image, and slot B marked BAD (untrusted until an update streams it). Every
# multi-byte field is little-endian, native to `STM32_Programmer_CLI -w` of a
# raw file. Flash the result at 0x08078000.
import struct, sys, zlib

img = open(sys.argv[1], "rb").read()          # e.g. g0b1-apu-slotA.bin
img_crc = zlib.crc32(img) & 0xFFFFFFFF

body = struct.pack(
    "<IHHI" "BBBBII" "BBBBII" "HHI",
    0x42434647, 1, 0, 1,                       # magic, version, active_slot=A, seq
    2, 0, 0, 0, len(img), img_crc,             # slot A: state=COMMITTED + pad, length, crc32
    0, 0, 0, 0, 0, 0,                          # slot B: state=BAD + pad, length=0, crc32=0
    0, 0, 0,                                   # trial_count, _pad, _pad2
)
assert len(body) == 44
crc = zlib.crc32(body) & 0xFFFFFFFF
record = body + struct.pack("<I", crc)
assert len(record) == 48

out = sys.argv[2] if len(sys.argv) > 2 else "bootcfg-seed.bin"
open(out, "wb").write(record)
print(f"wrote {out}: slotA length={len(img)} crc32=0x{img_crc:08X} record_crc=0x{crc:08X}")
