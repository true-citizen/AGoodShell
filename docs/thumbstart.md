# Thumbstart

A tiny first stage for the Supercard flash (about 14 KB). It starts the SD card, loads
`SFW.GBA` from the root of the card into the SDRAM and boots it. The firmware that does the
real work (SuperFW, AGoodShell++) then lives on the SD card and is not limited by the size
of the flash. The firmware already knows how to start from the SDRAM, which is how a
SuperFW image is chainloaded as a game from its own browser, so it unpacks itself the
same way it would from the flash.

## Build

    make BOARD=chis thumbstart.gba     # also BOARD=sd and BOARD=lite

The result is `thumbstart.gba`, wrapped like a normal firmware image (same header, run
through `tools/fw-fixer.py`), so it can be flashed the way the firmware is. The firmware
that goes on the SD card is the usual `superfw.gba` of the same board, renamed to `SFW.GBA`.

## What it does

1. `rom_boot.S` (built with `-DTHUMBSTART`, no fonts/patches/assets) copies the loader to the EWRAM.
2. `src/thumbstart.c` starts the card (`supercard_driver.c`), mounts it (FatFs, read only)
   and reads `/SFW.GBA` in 32 KiB pieces to the SDRAM at 0x08000000 (up to 8 MiB).
3. It checks the file starts with an ARM branch, maps the SDRAM read only and does the same
   soft reset the firmware does to start a game.

Errors (no card, can't mount, no `SFW.GBA`, bad size or read error) are shown on screen,
A tries again.

## Notes

* Updating the firmware from the menu with a normal `superfw.gba` would overwrite
  Thumbstart in the flash; with Thumbstart installed, update by replacing `SFW.GBA`.
* The loader font is Unscii (`res/fonts/unscii-16.hex`), converted by `res/tsfontcv.py`.
