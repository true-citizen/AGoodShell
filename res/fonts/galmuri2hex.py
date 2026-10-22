#!/usr/bin/env python3
# Converts a Galmuri BDF (https://github.com/Quiple/Galmuri, SIL OFL 1.1) into
# the UNSCII-style .hex format used by generator.py (8 pixel wide cells, 16 rows).
# Only characters up to 8 pixels wide are converted, the rest fall back to
# whatever font comes before it in the generator's file list.
#
# ./galmuri2hex.py Galmuri9.bdf galmuri9-latin.hex
import sys

def parse(path):
    g, cur, bits, asc = {}, None, False, 0
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if not p:
            continue
        if p[0] == "FONT_ASCENT": asc = int(p[1])
        elif p[0] == "STARTCHAR": cur = {"rows": []}
        elif p[0] == "ENCODING": cur["enc"] = int(p[1])
        elif p[0] == "BBX": cur["bbx"] = tuple(int(x) for x in p[1:5])
        elif p[0] == "BITMAP": bits = True
        elif p[0] == "ENDCHAR":
            bits = False
            if cur["enc"] >= 0: g[cur["enc"]] = cur
        elif bits:
            cur["rows"].append(bin(int(p[0], 16))[2:].zfill(len(p[0]) * 4))
    return g, asc

RANGES = [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F), (0x180, 0x24F), (0x370, 0x3FF),
          (0x400, 0x4FF), (0x25B8, 0x25B8), (0x2610, 0x2611), (0x2BC5, 0x2BC8)]
DOWN = 1    # Baseline shift to center the glyphs vertically in the 16 row cell

g, asc = parse(sys.argv[1])
out = []
for a, b in RANGES:
    for code in range(a, b + 1):
        if code not in g:
            continue
        w, h, xo, yo = g[code]["bbx"]
        top = asc - (yo + h) + DOWN
        rows, ok = [0] * 16, w <= 8
        for i, r in enumerate(g[code]["rows"] if ok else []):
            if not 0 <= top + i < 16:
                ok = False
                break
            for x, ch in enumerate(r[:w]):
                if ch == "1":
                    rows[top + i] |= 0x80 >> x
        if ok:
            out.append("%04X:%s" % (code, "".join("%02X" % r for r in rows)))
open(sys.argv[2], "w").write("\n".join(out) + "\n")
