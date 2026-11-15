/*
 * Linker script of the MP3 player. The player is a small standalone GBA ROM that the firmware
 * loads like an emulator: ROM (this image), then a header and the .mp3 file.
 * The code runs from RAM (the decoder loop from the IWRAM, the rest from the EWRAM), the
 * constant tables are read from the ROM.
 */
ENTRY(_start)
MEMORY {
  ROM   : ORIGIN = 0x08000000, LENGTH = 1M
  EWRAM : ORIGIN = 0x02000000, LENGTH = 256K
  IWRAM : ORIGIN = 0x03000000, LENGTH = 32K
}
SECTIONS {
  .header : { KEEP(*(.header)) } > ROM
  .start  : { KEEP(*(.start)) } > ROM
  .rodata : ALIGN(4) { *(.rodata*) } > ROM

  /* Hot code (the polyphase filter bank, the IMDCT, Huffman decoding...) and its data */
  .iwram : ALIGN(4) {
    __iw_start = .;
    *synth.o(.text*) *imdct_l_arm.o(.text*) *layer3.o(.text*) *fixed.o(.text*) *bit.o(.text*)
    *(.iwram*)
    __iw_end = .;
  } > IWRAM AT> ROM
  __iw_lma = LOADADDR(.iwram);

  /* The picture viewer's hot code (inflate, the JPEG decoder, the output) takes the place of the
     MP3 decoder in the IWRAM: image_main() copies it there, the two are never used together. */
  .iwram_img ORIGIN(IWRAM) : ALIGN(4) {
    __iwi_start = .;
    *picojpeg.o(.text*) *m_png.o(.text*) *m_jpg.o(.text*) *m_image.o(.text*)
    __iwi_end = .;
  } AT> ROM
  __iwi_lma = LOADADDR(.iwram_img);

  .ewram : ALIGN(4) {
    __ew_start = .;
    *(.text*) *(.glue*) *(.data*) *(.ewram*)
    __ew_end = .;
  } > EWRAM AT> ROM
  __ew_lma = LOADADDR(.ewram);
  /* The ROM ends here (it has to be a multiple of 4) */
  .endmark : ALIGN(4) { LONG(0) } > ROM

  .ebss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } > EWRAM
  .ibss (NOLOAD) : ALIGN(4) { __ibss_start = .; *(.ibss*) __ibss_end = .; } > IWRAM
}
