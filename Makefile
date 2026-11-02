
VERSION_WORD := 0x00000016
VERSION_SLUG_WORD := $(shell git rev-parse --short=8 HEAD || echo FFFFFFFF)

PREFIX		:= arm-none-eabi-
CC		:= $(PREFIX)gcc
CXX		:= $(PREFIX)g++
OBJDUMP		:= $(PREFIX)objdump
OBJCOPY		:= $(PREFIX)objcopy

COMPRESSION_RATIO ?= 4

GLOBAL_DEFINES = -D__GBA__

# BOARD can be "sd", "lite", "chis"
BOARD ?= sd

# Where the firmware runs from. With SDBOOT=1 (the default) the firmware is a file on the SD
# card (/SFW.GBA) that the small Thumbstart bootloader (see docs/thumbstart.md) loads to the
# SDRAM and starts, so the size of the flash of the cartridge doesn't limit it: the image can
# be up to 8 MiB, no feature is left out and all the emulators are bundled, for every board.
# SDBOOT=0 builds the firmware to be flashed directly, which has to fit the flash of the
# cart (the SD and Lite builds lose features to fit; discouraged).
SDBOOT ?= 1

ifeq ($(BOARD),lite)
  GLOBAL_DEFINES += -DSUPERCARD_LITE_IO
  COMPRESS_FIRMWARE = 1
  MAXFSIZE = 496
  FWFLAVOUR = "Lite"
  ifneq ($(SDBOOT),1)
    COMPRESSION_RATIO = 9
  endif
else ifeq ($(BOARD),sd)
  GLOBAL_DEFINES += -DSUPERCARD_FLASH_ADDRPERM
  COMPRESS_FIRMWARE = 1
  MAXFSIZE = 512
  FWFLAVOUR = "SD"
  ifneq ($(SDBOOT),1)
    GLOBAL_DEFINES += -DNO_CLASSIC_CAROUSEL -DCART_FEW_PICS
    BUNDLE_GBC_EMULATOR = 1
    # The image is very close to the limit, so spend more time compressing it.
    COMPRESSION_RATIO = 9
  endif
else ifeq ($(BOARD),chis)
  GLOBAL_DEFINES += -DSUPPORT_NORGAMES -DSUPERCHIS_IO -DFONTS_EXT
  BUNDLE_GBC_EMULATOR = 1
  BUNDLE_OTHER_EMULATORS = 1
  # Can't be over 2MiB (hardlimit)
  MAXFSIZE = 2048
  FWFLAVOUR = "Chis"
else
  $(error No valid board specified in BOARD)
endif

# Size limit used when linking the image. MAXFSIZE (the flash of the cartridge) is still what
# the firmware knows as its own maximum size, so it won't try to flash an image that is too big.
ifeq ($(SDBOOT),1)
  LINKFSIZE = 8192
  BUNDLE_GBC_EMULATOR = 1
  BUNDLE_OTHER_EMULATORS = 1
else
  LINKFSIZE = $(MAXFSIZE)
endif

FWBINFILES=firmware.ewram.gba res/patches.db res/fonts.pack

ifeq ($(ENABLE_DISK_LOGGING),1)
  PAYLOADFLAGS += -DENABLE_DISK_LOGGING
endif
ifeq ($(ENABLE_EMU_LOGGING),1)
  PAYLOADFLAGS += -DENABLE_EMU_LOGGING
endif
ifeq ($(ENABLE_UART_LOGGING),1)
  PAYLOADFLAGS += -DENABLE_UART_LOGGING
endif

ifeq ($(COMPRESS_FIRMWARE),1)
  GLOBAL_DEFINES += -DCOMPRESS_FONTS -DCOMPRESS_PATCHES -DCOMPRESS_FIRMWARE
  FWBINFILES := $(addsuffix .comp,$(FWBINFILES))
endif

ifeq ($(BUNDLE_GBC_EMULATOR),1)
  GLOBAL_DEFINES += -DBUNDLE_GBC_EMULATOR
  BIEMUFILES += emu/jagoombacolor_v0.5.gba.comp
endif

ifeq ($(BUNDLE_OTHER_EMULATORS),1)
  GLOBAL_DEFINES += -DBUNDLE_OTHER_EMULATOR
  BIEMUFILES += emu/pocketnes_20130701.gba.comp \
                emu/wasabigba_v0.2.4.gba.comp \
                emu/NGPGBA_v0.5.7.gba.comp \
                emu/pceadvance-v7.5-scptch.gba.comp \
                emu/smsadvance-v2.5-scptch.gba.comp
  # The MP3 player rides along with the other emulators (see docs/multimedia.md).
  GLOBAL_DEFINES += -DBUNDLE_MP3_PLAYER
  BIEMUFILES += emu/mp3player.gba.comp
endif

BASEFLAGS=$(GLOBAL_DEFINES) -mcpu=arm7tdmi -mtune=arm7tdmi

CFLAGS=-O2 -ggdb \
       $(BASEFLAGS) $(PAYLOADFLAGS) \
       -DFW_MAX_SIZE_KB=$(MAXFSIZE) -DFW_FLAVOUR="\"$(FWFLAVOUR)\"" \
       -DSC_FAST_ROM_MIRROR="use_fast_mirror()" \
       -DSD_PREERASE_BLOCKS_WRITE \
       -DVERSION_WORD="$(VERSION_WORD)" \
       -DVERSION_SLUG_WORD="0x$(VERSION_SLUG_WORD)" \
       -Wall -Isrc -I. -mthumb -flto -flto-partition=none


INGAME_CFLAGS=-Os -ggdb \
              $(BASEFLAGS) \
              -DNO_SUPERCARD_INIT \
              -DSD_PREERASE_BLOCKS_WRITE \
              -Wall -Isrc -I. \
              -mthumb -flto

DLDI_CFLAGS=-Os -ggdb \
            $(BASEFLAGS) -DSDDRV_TIMEOUT_MULT=2 \
            -DSD_PREERASE_BLOCKS_WRITE \
            -Wall -Isrc -I. \
            -mthumb -flto -fPIC

DIRECTSAVE_CFLAGS=-Os -ggdb \
                 $(BASEFLAGS) -DSDDRV_TIMEOUT_MULT=16 \
                 -DNO_SUPERCARD_INIT \
                 -Wall -Isrc -I. \
                 -marm -flto -fPIC

FATFSFILES=fatfs/diskio.c \
           fatfs/ff.c \
           fatfs/ffsystem.c \
           fatfs/ffunicode.c

DLDIFILES=src/dldi.S \
          src/dldi_driver.c \
          src/crc.c \
          src/supercard_io.S \
          src/supercard_driver.c

DIRECTSAVEFILES=src/directsaver.S \
                src/directsave_emu.c \
                src/crc.c \
                src/supercard_driver.c

MENUFILES=src/ingame.S \
          src/ingame_menu.c \
          src/cimpl.c \
          src/fonts/font_render.c \
          src/save.c \
          src/util.c \
          src/utf_util.c \
          src/fileutil.c \
          src/crc.c \
          src/nanoprintf.c \
          src/supercard_io.S \
          src/supercard_driver.c \
          ${FATFSFILES}

INFILES=src/gba_ewram_crt0.S \
        src/main.c \
        src/log.c \
        src/cimpl.c \
        src/settings.c \
        src/loader.c \
        src/save.c \
        src/patchengine.c \
        src/patcher.c \
        src/patches.S \
        src/menu.c \
        src/cartridge.c \
        src/recent.c \
        src/cheats.c \
        src/flash.c \
        src/sha256.c \
        src/misc.c \
        src/util.c \
        src/utf_util.c \
        src/emu.c \
        src/fileutil.c \
        src/asmutil.S \
        src/gbahw.c \
        src/virtfs.c \
        src/flash_mgr.c \
        src/binassets.S \
        src/crc.c \
        src/nds_loader.c \
        src/dldi_patcher.c \
        src/supercard_driver.c \
        src/supercard_io.S \
        src/heapsort.c \
        src/nanoprintf.c \
        src/fonts/font_render.c \
        ${FATFSFILES}

all:	$(FWBINFILES) $(BIEMUFILES) directsave.payload ingame_trampoline.payload langpacks
	# Wrap the firmware around a ROM->EWRAM loader
	$(CC) $(CFLAGS) -o firmware.elf rom_boot.S -T ldscripts/gba_romboot.ld -nostartfiles -nostdlib -Wl,--defsym,MAX_FLASH_SIZE=$(LINKFSIZE)K
	$(OBJCOPY) --output-target=binary firmware.elf superfw.gba
	# Fix the header/checksum.
	./tools/fw-fixer.py superfw.gba

# Thumbstart: a small first stage that boots /SFW.GBA from the SD card (see src/thumbstart.c)
THUMBSTARTFILES=src/gba_ewram_crt0.S \
        src/thumbstart.c \
        src/cimpl.c \
        src/supercard_driver.c \
        src/supercard_io.S \
        src/asmutil.S \
        src/crc.c \
        fatfs/diskio.c \
        fatfs/ff.c \
        fatfs/ffunicode.c

ldscripts/gba_thumbstart.ld.i: ldscripts/gba_ewram.ld
	cpp -DTHUMBSTART $< -o $@

thumbstart.ewram.gba: $(THUMBSTARTFILES) ldscripts/gba_thumbstart.ld.i
	$(CC) $(CFLAGS) -DTHUMBSTART -o thumbstart.ewram.elf $(THUMBSTARTFILES) -T ldscripts/gba_thumbstart.ld.i -nostartfiles -Wl,-Map=thumbstart.ewram.map -Wl,--print-memory-usage -fno-builtin
	$(OBJCOPY) --output-target=binary thumbstart.ewram.elf thumbstart.ewram.gba

# MP3 player: a small GBA ROM (see mp3/player.c) that the firmware loads like an emulator for .mp3
# files. libmad (third_party/libmad) decodes. The hot code is ARM (it uses SMULL), the rest Thumb.
MADDIR = third_party/libmad
MADFILES = bit fixed frame huffman layer3 stream synth timer
MP3FLAGS = -O2 -mcpu=arm7tdmi -mtune=arm7tdmi -mthumb-interwork -ffunction-sections -fno-builtin -w \
           -DFPM_ARM -DOPT_SPEED -DOPT_SSO -DASO_IMDCT -DASO_ZEROCHECK -DNDEBUG -I$(MADDIR) -Isrc -Imp3
mp3player.gba:	mp3/player.c mp3/irq.c mp3/stubs.c mp3/crt0.S mp3/mp3.ld mp3/player.h src/thumbstart_font.h \
		$(addprefix $(MADDIR)/,$(addsuffix .c,$(MADFILES))) $(MADDIR)/imdct_l_arm.S
	rm -rf obj-mp3; mkdir -p obj-mp3
	for f in $(MADFILES); do $(CC) $(MP3FLAGS) -marm -c $(MADDIR)/$$f.c -o obj-mp3/$$f.o || exit 1; done
	$(CC) $(MP3FLAGS) -marm -c mp3/irq.c -o obj-mp3/irq.o
	$(CC) $(MP3FLAGS) -mthumb -c mp3/player.c -o obj-mp3/player.o
	$(CC) $(MP3FLAGS) -mthumb -c mp3/stubs.c -o obj-mp3/stubs.o
	$(CC) $(BASEFLAGS) -mcpu=arm7tdmi -marm -Isrc -I. -c $(MADDIR)/imdct_l_arm.S -o obj-mp3/imdct_l_arm.o
	$(CC) -mcpu=arm7tdmi -c mp3/crt0.S -o obj-mp3/crt0.o
	$(CC) -nostartfiles -nostdlib -T mp3/mp3.ld obj-mp3/*.o -lgcc -o mp3player.elf -Wl,-Map=mp3player.map
	$(OBJCOPY) --output-target=binary mp3player.elf mp3player.gba
	./tools/fw-fixer.py mp3player.gba

emu/mp3player.gba.bin:	mp3player.gba
	cp mp3player.gba $@

thumbstart.ewram.gba.comp: thumbstart.ewram.gba ./upkr.elf
	./upkr.elf -l $(COMPRESSION_RATIO) $< $@

thumbstart.gba: thumbstart.ewram.gba.comp
	# Wrap it around the same ROM->EWRAM loader as the firmware (no fonts, patches or assets)
	$(CC) $(CFLAGS) -DTHUMBSTART -DCOMPRESS_FIRMWARE -DFIRMWARE_PAYLOAD='"thumbstart.ewram.gba"' -DFIRMWARE_PAYLOAD_COMP='"thumbstart.ewram.gba.comp"' -o thumbstart.elf rom_boot.S -T ldscripts/gba_romboot.ld -nostartfiles -nostdlib -Wl,--defsym,MAX_FLASH_SIZE=$(MAXFSIZE)K
	$(OBJCOPY) --output-target=binary thumbstart.elf thumbstart.gba
	./tools/fw-fixer.py thumbstart.gba

firmware.ewram.gba: $(INFILES) ingamemenu.payload superfw.dldi.payload directsave.payload ingame_trampoline.payload src/messages_data.h ldscripts/gba_ewram.ld.i
	# Build the actual firmware image
	$(CC) $(CFLAGS) -o firmware.ewram.elf $(INFILES) -T ldscripts/gba_ewram.ld.i -nostartfiles -Wl,-Map=firmware.ewram.map -Wl,--print-memory-usage -fno-builtin
	$(OBJCOPY) --output-target=binary firmware.ewram.elf firmware.ewram.gba

superfw.dldi.payload:	$(DLDIFILES)
	# Build in-game menu
	$(CC) $(DLDI_CFLAGS) -o superfw.dldi.elf $(DLDIFILES) -T ldscripts/gba_dldi.ld \
			-nostartfiles -fno-builtin -Wl,-Map=superfw.dldi.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary superfw.dldi.elf superfw.dldi.payload

directsave.payload:	$(DIRECTSAVEFILES)
	$(CC) $(DIRECTSAVE_CFLAGS) -o directsave.elf $(DIRECTSAVEFILES) -T ldscripts/gba_directsave.ld \
			-nostartfiles -fno-builtin -Wl,-Map=directsave.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary directsave.elf directsave.payload

ingamemenu.payload:	$(MENUFILES) src/menu_messages.h
	# Build in-game menu
	$(CC) $(INGAME_CFLAGS) -o ingamemenu.elf $(MENUFILES) -T ldscripts/gba_ingame.ld \
			-nostartfiles -fno-builtin -Wl,-Map=firmware.ingame.map -Wl,--print-memory-usage
	$(OBJCOPY) --output-target=binary ingamemenu.elf ingamemenu.payload

ingame_trampoline.payload:	src/ingame_trampoline.S
	$(CC) $(BASEFLAGS) -nostartfiles -T ldscripts/gba_ingametramp.ld -o ingame_trampoline.elf src/ingame_trampoline.S
	$(OBJCOPY) --output-target=binary ingame_trampoline.elf ingame_trampoline.payload

src/messages_data.h:	res/messages.py $(wildcard res/lang/*.json)
	./res/messages.py h main > src/messages_data.h

# Translation packs (everything but English), to be copied to /.superfw/lang/ on the SD card.
# They depend on the board, as some strings only exist in some (see SUPPORT_* defines).
LANGS = es fr it de ca pt cs zh ko uk ru id ms
LANGDEFS = $(patsubst -D%,%,$(filter -DSUPPORT_%,$(GLOBAL_DEFINES)))
lang/%.lng:	res/messages.py res/lang/%.json
	@mkdir -p lang
	./res/messages.py lng $* $@ $(LANGDEFS)
langpacks:	$(addprefix lang/,$(addsuffix .lng,$(LANGS)))

# Everything that goes to the root of the SD card, laid out like it (for the SDBOOT=1 setup).
sdcard:	all
	rm -rf sdcard
	mkdir -p sdcard/.superfw/lang
	cp superfw.gba sdcard/SFW.GBA
	cp lang/*.lng sdcard/.superfw/lang/

src/menu_messages.h:	res/messages.py
	./res/messages.py h menu > src/menu_messages.h

firmware.ewram.gba.comp:	firmware.ewram.gba ./upkr.elf
	./upkr.elf -l $(COMPRESSION_RATIO) $< $@

%.gba.comp:	%.gba.bin ./upkr.elf
	./upkr.elf -l $(COMPRESSION_RATIO) $< $@

%.db.comp:	%.db ./upkr.elf
	./upkr.elf -l $(COMPRESSION_RATIO) $< $@

%.pack.comp:	%.pack apultra.elf
	./apultra.elf $< $@

%.ld.i:	%.ld
	cpp $< -o $@

apultra.elf:	tools/apultra.cc
	g++ -std=c++20 -O3 $< -o $@

upkr.elf:	tools/upkr.cc
	g++ -o $@ $< -O3 -ffast-math

clean:
	rm -rf lang sdcard; rm -f ldscripts/*.i *.gba *.elf *.payload *.map res/*.comp emu/*.comp *.comp src/menu_messages.h src/messages_data.h

