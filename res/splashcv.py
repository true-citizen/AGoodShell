#!/usr/bin/env python3
# Converts the boot splash screen (res/splash.png, 240x160) for rom_boot.S:
#   splashcv.py res/splash.png res/splash.raw res/splash.pal
# splash.raw is the 8 bit indexed picture (240x160 bytes, compressed later with upkr), splash.pal
# the palette (12 colors, 15 bit). The picture can have up to 12 colors. Black is made color 0 (the
# background the firmware clears its start-up messages to) and white color 5 (the color it draws
# them with, see display_info_msg in src/main.c).
import sys, struct, PIL.Image
from collections import Counter

im = PIL.Image.open(sys.argv[1]).convert("RGB")
assert im.size == (240, 160), im.size
cols = [c for c, _ in Counter(im.getdata()).most_common()]
assert len(cols) <= 12, "the splash screen can have 12 colors at most (it has %d)" % len(cols)
assert (0, 0, 0) in cols and (255, 255, 255) in cols, "needs black and white"
pal = [(0, 0, 0)] + [c for c in cols if c not in ((0, 0, 0), (255, 255, 255))]
pal.insert(5, (255, 255, 255))
pal = (pal + [(0, 0, 0)] * 12)[:12]
idx = {}
for i, c in enumerate(pal):
  idx.setdefault(c, i)
open(sys.argv[2], "wb").write(bytes(idx[p] for p in im.getdata()))
open(sys.argv[3], "wb").write(b"".join(struct.pack("<H", (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)) for r, g, b in pal))
