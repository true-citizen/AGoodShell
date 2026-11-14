#!/usr/bin/env python3
# Converts the boot splash screen (res/splash.png, 240x160) for rom_boot.S:
#   splashcv.py res/splash.png res/splash.raw res/splash.pal
# splash.raw is the 8 bit indexed picture (240x160 bytes, compressed later with upkr), splash.pal
# the palette (12 colors, 15 bit). The picture can have up to 12 colors. The firmware clears its
# start-up messages to color 0 and draws them with color 5 (see display_info_msg in src/main.c),
# so the most common color (the background) becomes color 0 and color 5 is the color that
# contrasts most with it (black on a light picture, white on a dark one).
import sys, struct, PIL.Image
from collections import Counter

im = PIL.Image.open(sys.argv[1]).convert("RGB")
assert im.size == (240, 160), im.size
cols = [c for c, _ in Counter(im.getdata()).most_common()]
assert len(cols) <= 12, "the splash screen can have 12 colors at most (it has %d)" % len(cols)
def lum(c): return 299 * c[0] + 587 * c[1] + 114 * c[2]
bg = cols[0]
text = max(cols[1:], key=lambda c: abs(lum(c) - lum(bg))) if len(cols) > 1 else bg
rest = [c for c in cols if c not in (bg, text)]
pal = ([bg] + rest[:4] + [(0, 0, 0)] * 4)[:5]     # Colors 0 to 4
pal += [text] + rest[4:]              # Color 5, then the others
pal = (pal + [(0, 0, 0)] * 12)[:12]
idx = {}
for i, c in enumerate(pal):
  idx.setdefault(c, i)
open(sys.argv[2], "wb").write(bytes(idx[p] for p in im.getdata()))
open(sys.argv[3], "wb").write(b"".join(struct.pack("<H", (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)) for r, g, b in pal))
