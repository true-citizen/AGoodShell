# Cartridge view: pictures, box art and plastic colors

The cartridge carousel shows a picture of the cartridge of the platform of each file.
The pictures are the ones drawn for this project (`res/carts/*.png`, 240x160, used unmodified).
`res/cartcv.py` converts them into `src/res/cartridge.h`, see the comment at the top of the script
for the exact command (it needs `upkr.elf`, built by `make upkr.elf`).

In each picture, pure green marks the label window, where the box art goes (rounded corners are
respected). Plastic, its highlight and its shadow follow the plastic color of the game; the yellow
contacts of some cartridges keep their color.

| Extension     | Cartridge             | Box art size |
|---------------|-----------------------|--------------|
| gba (and any other file) | GBA        | 150x80       |
| gb            | Game Boy              | 96x84        |
| gbc           | Game Boy Color        | 98x70        |
| nes           | NES                   | 55x90        |
| sv            | Watara Supervision    | 186x28       |
| gg            | Game Gear             | 116x62       |
| pce           | PC Engine             | 104x90       |
| ngc           | Neo Geo Pocket Color  | 76x68        |
| sms, sg       | Master System / SG-1000 | 100x66     |

The SD build is very short of flash space and only has the GBA, GB, GBC and NES pictures (the
others show the GBA cartridge).

## Box art

BMP files (uncompressed, 8, 24 or 32 bits per pixel, at most 256x256) in `/ART` on the SD card,
named after the ROM without the extension (`Game.bmp` for `Game.gba`), or for GBA ROMs after the
4 character game code (`AXVE.bmp`). A BMP of a different size than the table is scaled to it.

## Plastic color

`/ART/colors.txt`, one `name=color` per line, where name is the ROM name without the extension or the
GBA game code, and color is black, gray, white, red, orange, yellow, gold, green, teal, blue, purple,
pink or 6 hex digits.
