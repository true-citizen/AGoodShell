AGoodShell++
=======
<img width="240" height="160" alt="menu-screen" src="https://github.com/user-attachments/assets/2da87b1d-3446-4bac-9464-aea11a1ac60c" />
<img width="240" height="160" alt="calendar-screen" src="https://github.com/user-attachments/assets/3807c127-fd14-4d27-9f2c-5b08dc8aa927" />
<img width="240" height="160" alt="menu-dark" src="https://github.com/user-attachments/assets/d779009b-ee99-41f7-a905-ffdfd262c865" />
<img width="240" height="160" alt="dsi-menu" src="https://github.com/user-attachments/assets/7fbd556a-daf3-46c1-ae7c-3cb54e21608a" />
<img width="240" height="160" alt="cart-menu" src="https://github.com/user-attachments/assets/c837e637-b183-4480-a30e-475c16bf289e" />
<img width="240" height="160" alt="media-player" src="https://github.com/user-attachments/assets/da64d2d5-8778-45e5-bcc7-441ee5269b54" />
<img width="240" height="160" alt="about-screen" src="https://github.com/user-attachments/assets/79b29954-a044-422f-a9c8-e9978b419dae" />

A fork of SuperFW firmware for Supercard GBA flash carts (and derivatives/clones)

This project aims to bring DS-like interface to Gameboy Advance gaming console.

As this is still a recognizable fork of SuperFW under the hood, 
its website and documentation are at https://superfw.davidgf.net/

<b>Warning: I have limited testing opportunities(only SuperChis and DS Phat, 
but not original SuperCard nor ordinary GBA), so please report all issues/regresses
you found that are not present on official SuperFW on Issues tab of this fork. Its preferable that
besides of GBA you also have Nintendo DS/DS Lite so in case if your flashcart gets bricked, 
you could reflash original SuperFW back using Nintendo DS.</b>
<details>
<summary>Click to expand</summary>
 
Installation
------------

Documentation for the original SuperFW is at https://superfw.davidgf.net/docs/install/flash/
but this fork recommends a different main method, described below.

### Recommended: Thumbstart bootloader + sfw.gba on the SD card

The firmware is too big for the flash of SuperCard/SuperChis due to added features, 
so the recommended setup is split in two:

 1. Flash the tiny **Thumbstart** bootloader (`thumbstart.gba`, built with
    `make BOARD=<chis|sd|lite> thumbstart.gba`, see docs/thumbstart.md) into
    the cartridge's flash. It is only a few KB.
 2. Copy the firmware to the **root of the SD card** with the name `sfw.gba`
    (`SFW.GBA`). Thumbstart loads it from there on every boot (chain-loading).
    `make BOARD=<chis|sd|lite> sdcard` builds everything and lays it out in an
    `sdcard/` folder that you can copy to the card as it is. It contains `SFW.GBA`
    and the translations (`.superfw/lang/*.lng`, one file per language; without
    them the menu is in English). 

Flash Thumbstart the same way you would flash any other firmware (see below:
from the Info tab of a running SuperFW/SCFW, or using an NDS). After that,
updating is just replacing `sfw.gba` on the SD card, no flashing needed. This
also removes risks related to repeated flashing and the flash size limit: 
all builds have all features for any cartridge. See docs/sd-boot.md. 

### Recovery / flashing with an NDS

Flashing is also possible using an NDS. This is particularly useful if you
_brick_ your Supercard (ie. interrupting flashing, low battery conditions
and similar situations could cause a bad flash). You will need an NDS device
and a Slot-1 cart as well. Download the .nds ROM for your Slot-1 cart at
https://github.com/davidgfnet/superfw-nds-flasher-tool/releases/ and launch
it with your Supercard on your Slot-2. You should be able to flash (as well
as backup) your flash.

GB/GBC Emulation
----------------

GameBoy and GameBoy Color ROMs can be played by using the built-in Goombacolor
emulator binary (the Lite build doesn't ship any emulator though). Picking any
.gb/.gbc file will load the emulator and the ROM and start its execution.

Other devices can also be played as long as the right emulator is installed in
the SD card (and supported by SuperFW).

Check https://superfw.davidgf.net/docs/usermanual/emulators/ for details.

ROM patching
------------

The firmware contains a patch database to patch several features. A custom
database can also be loaded from the SD card and used instead (so more games
and improvements can be added). The patches contain information about:

 - WaitCNT patches: Also called white/black screen patches, prevent games from
   updating the WAITCNT waitstates (the supercard has a slow memory). Without
   a correct patch the game won't even boot.
 - Flash/EEPROM offsets: Indicate where the relevant storage routines are so
   that they can be patched and converted to SRAM storage.
 - IRQ handler patches: Used to patch user IRQ handler routine and install a
   custom one. Used to enable in-game menu.
 - RTC patches: Used for games that contained an RTC IC in theri cart, to keep
   track of time (both time and date). There's only a handful such ROMs.

More information at https://superfw.davidgf.net/docs/usermanual/patches/

These patches are generated mostly automatically, check out the patch repo at:
https://github.com/davidgfnet/gba-patch-gen
It is also possible to use the web-based patch generator for better patches:
https://patchtool.superfw.davidgf.net/

Special AGoodShell++ features
------------
AGoodShell++ contains functionalities not present in original SuperFW:
 - Opening .mp3, .png, .gif, .jpg, .bmp files
 - DSi-like file manager with icons and Cartridge-art mode of file manager that supports GBA and all emulated consoles cartridge arts. (Cartridge art images are not included). Please read https://github.com/true-citizen/AGoodShell/blob/master/docs/cartridges.md
 - DS menu and calendar
 - Dark/Light theme
 - Revised settings menu

In-game menu
------------

SuperFW features an in-game menu that allows users to pause the current game
and perform certain actions such as:

  - Resuming and resetting the game
  - Going back to the SuperFW menu (witout having to reboot your GBA)
  - Handling saves (for games that allow saving)
  - Creating and restoring savestates
  - Applying/using cheat codes
  - Changing the RTC time (for games that use an RTC)

This menu is a bit of a hack that requires patching the ROM to work. For this
reason, some games won't work well with it or will suffer from bugs (usually
graphical bugs). In this case it is advised to not use the in-game menu.

Many graphical glitches will result in the screen being "offseted" to the
left/right/up/down. In many cases this is not an issue (besides making it
harder for the user to see and play) and it goes away when entering a new
zone/level/menu. This is due to the GBA featuring some "write-only" registers,
that is, registers that can be written but never read back. For this reason
we cannot properly save and restore said registers.

Saving games
------------

Save games are stored in the cart's SRAM and preserved by the cart battery
(note that if the battery is dead the game will be lost). On reboot SuperFW
will write the savegame to the SD card to preserve it and allow loading
another save game.

When using the in-game menu, you might enter the menu and select any of the
saving options, which will write the save to the SD card. This is a good
way to save your games if you prefer to manually handle save files (ie.
disabling autosave and manually choosing when to save).

For Flash-based games (around 300 games) and EEPROM-based games (around 1400
games) it is possible to patch games so that they write directly to the SD
save game, this is called Direct-Saving mode. This makes saving more reliable
(no need for a battery!) and simpler to use (no need to reboot to ensure
saving or using the in-game menu). Games that use Flash or EEPROM will display
an option for direct-saving (this is the default choice in Auto mode).

Files and configuration on the SD card
--------------------------------------

All SuperFW related files are stored under "/.superfw" at the root of the card.
The following files are usually created:

 - .superfw/settings.txt: User settings, loaded on startup.
 - .superfw/ui-settings.txt: UI settings, loaded on startup.
 - .superfw/recent.txt: Recently played ROMs, in order.
 - .superfw/pending-save.txt: SRAM save information (temp file).
 - .superfw/pending-sram-test.txt: SRAM test flag (temp file).

Other noteworthy paths:

 - .superfw/config/: Per-ROM load configuration.
 - .superfw/patches/: Patch cache (created by PatchEngine).
 - .superfw/cheats/: Cheat database, contains .cht files.
 - .superfw/emulators/: Emulator ROMs, used to play other device's ROMs.

Limits
------

The following restrictions apply to the firmware due to memory/storage/cpu
constraints:

 - Maximum ROM size: 32MiB (Supercard's memory size)
 - File path and name limit: 255 utf-8 bytes (not exactly characters!)
 - Maximum number of files+dirs in a directory: 16384
 - MP3 player can't play audio in quality higher than Mono 8 KHz 16 kbps
 - I don't recommend opening .bmp, .jpg, .gif, .png images in high-resolutions to avoid any problems from that
</details>

Licenses
--------

Most of SuperFW was written by davidgf and is published under GPL license.
Some components use third party code, such as: nanoprintf (public domain),
heapsort (3-BSD) and fatfs (1-BSD-like). apultra and upkr (only used at
build-time) were re-implemented in C++ using the original sources as reference
and an LLM (they are under zlib and public domain respecitvely). Some
linkerscript/crt0 code was adapted from AntonioND's work under CC0.
This specific forks also employs some elements from TwiLightMenu++(GPL-3.0),
Galmuri font by quiple(OFL-1.1). LLM tools are employed in development, but
graphic art is drawn by me where applicable.

Special credits
--------
 - SuperFW and TWiLightMenu++ developers
 - Piece of 99% chocolate
 - You, for downloading this fork
