#!/usr/bin/env python3
# Builds a ROM that plays an .mp3 in an emulator: the player ROM, the header the firmware
# writes after an emulator (see mp3_header() in src/emu.c) and the file.
# Usage: mktest.py mp3player.gba file.mp3 out.gba
import struct, sys
rom = open(sys.argv[1], "rb").read()
mp3 = open(sys.argv[2], "rb").read()
size = struct.unpack_from("<I", rom, 0xCC)[0]
assert size == len(rom), (size, len(rom))
hdr = b"AMP3" + struct.pack("<I", len(mp3)) + sys.argv[2].split("/")[-1].encode()[:31].ljust(32, b"\0")
out = rom + hdr + mp3
out += b"\0" * (-len(out) % 4)
out += b"\0" * max(0, 512 * 1024 - len(out))      # Emulators treat small ROMs as multiboot images
open(sys.argv[3], "wb").write(out)
