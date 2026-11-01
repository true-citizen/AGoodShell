#!/usr/bin/env python3
"""Converts a picture into box art for the cartridge view of the game browser.

The art is a 150x80 BMP (24 bits per pixel) in the ART folder at the root of the SD
card, named after the ROM without its extension (ART/Game.bmp for Game.gba) or after
the 4 character game code of a GBA ROM (ART/AXVE.bmp). The picture is reduced to the
colors the menu can show (5 reds x 6 greens x 3 blues) with error diffusion, which
looks a lot better than what the firmware does with any other BMP.

  artconv.py cover.png ART/                    -> ART/cover.bmp
  artconv.py cover.png --rom "Game.gba" ART/   -> ART/<game code of the ROM>.bmp
  artconv.py cover.png --id AXVE ART/          -> ART/AXVE.bmp
  artconv.py cover.png ART/Game.bmp            -> exact output file

By default the picture is scaled to cover the whole 150x80 area and cropped in the
middle; --fit keeps all of it and fills the borders with black.

Plastic colors go in ART/colors.txt, one "name=color" per line, where name is the ROM
name without extension or its game code, and color one of: black gray white red orange
yellow gold green teal blue purple pink, or 6 hex digits (#c82828).
"""
import argparse, os, struct, sys
from PIL import Image

W, H = 150, 80
LV = (5, 6, 3)

def write_bmp(path, im):
    px = im.load()
    stride = (W * 3 + 3) & ~3
    rows = []
    for y in range(H - 1, -1, -1):           # Bottom to top
        r = bytearray()
        for x in range(W):
            rr, gg, bb = px[x, y]
            r += bytes((bb, gg, rr))
        r += b"\0" * (stride - W * 3)
        rows.append(bytes(r))
    data = b"".join(rows)
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + data)

def dither(im):
    # Floyd-Steinberg, serpentine, to the palette levels of the firmware.
    px = [[list(map(float, im.getpixel((x, y)))) for x in range(W)] for y in range(H)]
    out = Image.new("RGB", (W, H))
    for y in range(H):
        xs = range(W) if y % 2 == 0 else range(W - 1, -1, -1)
        d = 1 if y % 2 == 0 else -1
        for x in xs:
            old = px[y][x]
            new = []
            for c in range(3):
                v = min(255.0, max(0.0, old[c]))
                lv = LV[c] - 1
                new.append(int(round(round(v * lv / 255.0) * 255.0 / lv)))
            out.putpixel((x, y), tuple(new))
            err = [old[c] - new[c] for c in range(3)]
            for dx, dy, f in ((d, 0, 7 / 16), (-d, 1, 3 / 16), (0, 1, 5 / 16), (d, 1, 1 / 16)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < W and 0 <= yy < H:
                    for c in range(3):
                        px[yy][xx][c] += err[c] * f
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("output", help="output folder (usually ART) or .bmp file")
    ap.add_argument("--rom", help="name the art after the game code of this GBA ROM")
    ap.add_argument("--id", help="name the art after this ID (game code or ROM name without extension)")
    ap.add_argument("--fit", action="store_true", help="keep the whole picture instead of cropping")
    a = ap.parse_args()

    im = Image.open(a.image).convert("RGBA")
    bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
    im = Image.alpha_composite(bg, im).convert("RGB")

    scale = (min if a.fit else max)(W / im.width, H / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))

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
    write_bmp(path, dither(canvas))
    print(path)

main()
