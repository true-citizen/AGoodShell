#!/usr/bin/env python3
# Builds a ROM that plays an .mp3 or shows a picture in an emulator: the media ROM, the header
# the firmware writes after an emulator (see media_header() in src/emu.c) and the file.
# Usage: mktest.py mediaplayer.gba file.mp3|png|jpg|gif out.gba
import struct, sys
rom = open(sys.argv[1], "rb").read()
mp3 = open(sys.argv[2], "rb").read()
size = struct.unpack_from("<I", rom, 0xCC)[0]
assert size == len(rom), (size, len(rom))
magic = b"AMP3" if sys.argv[2].lower().endswith(".mp3") else b"AIMG"
hdr = magic + struct.pack("<I", len(mp3)) + sys.argv[2].split("/")[-1].encode()[:31].ljust(32, b"\0")
out = rom + hdr + mp3
out += b"\0" * (-len(out) % 4)
out += b"\0" * max(0, 512 * 1024 - len(out))      # Emulators treat small ROMs as multiboot images
open(sys.argv[3], "wb").write(out)
