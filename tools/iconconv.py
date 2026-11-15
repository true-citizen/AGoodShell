#!/usr/bin/env python3
"""Converts a picture into a game icon for the DSi style carousel.

The icon is a 32x32 BMP (24 bits per pixel) in the ICON folder at the root of the SD card,
named after the ROM without its extension (ICON/Game.bmp for Game.gba) or after the
4 character game code of a GBA ROM (ICON/AXVE.bmp). Transparent areas (alpha in the
source picture) become pure magenta (FF00FF), which the firmware treats as transparent.
The colors are reduced to what the menu can show (5 reds x 6 greens x 3 blues) with error
diffusion; those colors are kept exactly if you draw the icon with them (see docs/cartridges.md).

  iconconv.py icon.png ICON/                    -> ICON/icon.bmp
  iconconv.py icon.png --rom "Game.gba" ICON/   -> ICON/<game code of the ROM>.bmp
  iconconv.py icon.png --id AXVE ICON/          -> ICON/AXVE.bmp
  iconconv.py icon.png ICON/Game.bmp            -> exact output file

Pictures that are not square are fitted in the middle (transparent borders); a picture that
is not 32x32 is scaled (nearest neighbour for exact multiples such as 64x64, Lanczos otherwise).
"""
import argparse, os, struct, sys
from PIL import Image

S = 32
LV = (5, 6, 3)
KEY = (255, 0, 255)

def write_bmp(path, im):
    px = im.load()
    stride = (S * 3 + 3) & ~3
    rows = []
    for y in range(S - 1, -1, -1):           # Bottom to top
        r = bytearray()
        for x in range(S):
            rr, gg, bb = px[x, y]
            r += bytes((bb, gg, rr))
        r += b"\0" * (stride - S * 3)
        rows.append(bytes(r))
    data = b"".join(rows)
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, S, S, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + data)

def quantize(v, c):
    lv = LV[c] - 1
    return int(round(round(min(255.0, max(0.0, v)) * lv / 255.0) * 255.0 / lv))

def dither(im, alpha):
    # Floyd-Steinberg, serpentine. Transparent pixels stay magenta and take no part.
    px = [[list(map(float, im.getpixel((x, y)))) for x in range(S)] for y in range(S)]
    out = Image.new("RGB", (S, S), KEY)
    for y in range(S):
        xs = range(S) if y % 2 == 0 else range(S - 1, -1, -1)
        d = 1 if y % 2 == 0 else -1
        for x in xs:
            if alpha.getpixel((x, y)) < 128:
                continue
            old = px[y][x]
            new = [quantize(old[c], c) for c in range(3)]
            if tuple(new) == KEY:                 # Never produce the transparent color by accident
                new[0] = 191
            out.putpixel((x, y), tuple(new))
            err = [old[c] - new[c] for c in range(3)]
            for dx, dy, f in ((d, 0, 7 / 16), (-d, 1, 3 / 16), (0, 1, 5 / 16), (d, 1, 1 / 16)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < S and 0 <= yy < S and alpha.getpixel((xx, yy)) >= 128:
                    for c in range(3):
                        px[yy][xx][c] += err[c] * f
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("output", help="output folder (usually ICON) or .bmp file")
    ap.add_argument("--rom", help="name the icon after the game code of this GBA ROM")
    ap.add_argument("--id", help="name the icon after this ID (game code or ROM name without extension)")
    a = ap.parse_args()

    im = Image.open(a.image).convert("RGBA")
    scale = min(S / im.width, S / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    exact = im.width % S == 0 and im.width == im.height
    im = im.resize((nw, nh), Image.NEAREST if exact else Image.LANCZOS)
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    canvas.paste(im, ((S - nw) // 2, (S - nh) // 2))
    alpha = canvas.getchannel("A")
    rgb = canvas.convert("RGB")

    if a.output.lower().endswith(".bmp"):
        path = a.output
    else:
        ident = a.id
        if a.rom:
            with open(a.rom, "rb") as f:
                f.seek(0xAC)
                ident = f.read(4).decode("ascii")
            if not ident.isalnum():
                sys.exit("%s has no valid game code" % a.rom)
        ident = ident or os.path.splitext(os.path.basename(a.image))[0]
        os.makedirs(a.output, exist_ok=True)
        path = os.path.join(a.output, ident + ".bmp")
    write_bmp(path, dither(rgb, alpha))
    print(path)

main()
