# Running from the SD card (SDBOOT) and the EWRAM budget

## What changed

By default (`SDBOOT=1`) the firmware is built to be started from the SD card as `/SFW.GBA` by the
Thumbstart bootloader (docs/thumbstart.md), which is the only thing in the flash of the cartridge.
The size of the flash (496 KiB on Lite, 512 KiB on SD, 2 MiB on Chis) doesn't limit the image any more:

* the link limit is 8 MiB (what Thumbstart loads), the same for every board;
* the SD and Lite builds have everything Chis has: the DSi carousel, all the cartridge pictures,
  all the bundled emulators;
* `make BOARD=<board> sdcard` lays out the card: `sdcard/SFW.GBA` and `sdcard/.superfw/lang/*.lng`.

`make SDBOOT=0` gives the old firmware that is flashed directly (with the SD and Lite trimming).
The firmware still knows the real flash size of the board (`FW_MAX_SIZE_KB`), so it refuses to flash
an image that doesn't fit.

## What EWRAM is and why it is the real limit

The GBA has two kinds of working RAM: 32 KiB of fast IWRAM (32-bit bus, used for the stack, the
variables and the time critical code) and 256 KiB of EWRAM (16-bit bus, slower).
The cartridge flash is also slow and, on a SuperCard, its bus is shared with the SD card
interface, so the firmware doesn't run from there. Instead the loader (`rom_boot.S`) unpacks the
whole firmware into the EWRAM and runs it from there. 5 KiB are reserved at the top (the firmware
may be started from a Nintendo DS, which keeps some of it), so the program (code and constant
data) can be 251 KiB at most (`ldscripts/gba_ewram.ld`). Whatever doesn't fit there can't be in
the firmware, however big the flash or the SD card is.

The SDRAM of the SuperCard (32 MiB, seen at 0x08000000) is different: it holds the data that is
big, the file list (`sdr_state` in `menu.c`), the fonts, the patch database and the bundled
emulators. It is read like a ROM, but a program can't run from it comfortably and it only takes
16 and 32 bit writes.

So in the 251 KiB there is code (about 150 KiB) and constant data (about 97 KiB before this
change), and moving things out of it means putting them in the SDRAM or the SD card.

## What was moved: translations

The strings of the 13 languages other than English were about 70 KiB of the EWRAM, all of it
dead weight for the user that reads the menu in one language. Now only English is built in (the
error screens work with no SD card) and the others are packs:
`/.superfw/lang/xx.lng`, made by `res/messages.py lng` (`make langpacks`). When the language is
selected (at start up and when changing it in the UI settings) the pack is read into the SDRAM
and the menu uses its strings from there. If the pack is missing or corrupt (or from a build
with other strings) the menu shows English.

Result (Chis): the EWRAM image went from 255 KB to 180 KB, 71 KB below the limit; the SD and Lite
builds are at 171 KB with all the features on.

## Other candidates

* Bundled emulators, fonts and the patch database are already in SDRAM, not in EWRAM.
* The cartridge pictures (1.5 KB, compressed) and the icons (about 3 KB) are small.
* Code that is rarely used (NOR flash management on Chis, the clock, the DSi carousel) could be
  run from the SDRAM or loaded on demand, but that needs a linker and loader change and costs
  speed. It isn't needed now that the strings are out.
