/*
 * Thumbstart: a minimal first stage for the Supercard flash. It starts the SD card,
 * loads /SFW.GBA (any SuperFW/AGoodShell++ image) into the SDRAM and boots it, so the
 * firmware itself is not limited by the size of the flash. The firmware recognizes
 * that it was started from the SDRAM and unpacks itself as it would from the flash.
 *
 * This runs from the EWRAM (see rom_boot.S), the way the firmware does.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gbahw.h"
#include "common.h"
#include "fatfs/ff.h"
#include "supercard_driver.h"
#include "thumbstart_font.h"

// Read by the SD driver (see supercard_driver.c)
bool isgba = true;
bool slowsd = false;

#define FW_FILE          "0:/SFW.GBA"
#define FW_MAX_SIZE      (8 * 1024 * 1024)       // Well under the fonts, patches and assets areas.

#define BUF_ADDR         0x02020000              // Free EWRAM (this program takes the first 128KiB)
#define BUF_SIZE         (32 * 1024)

#define RGB(r, g, b)     ((r) | ((g) << 5) | ((b) << 10))
#define COL_BG           RGB(2, 4, 10)
#define COL_TEXT         RGB(31, 31, 31)
#define COL_DIM          RGB(16, 18, 24)
#define COL_ERR          RGB(31, 12, 10)
#define COL_BAR          RGB(10, 24, 12)

// Interrupts are never enabled here (asmutil.S refers to the handler)
void irq_handler_fn() {}

static FATFS sdfs;

// ---------------------------------------------------------------------------
// Screen: mode 3 (16 bit bitmap), text with the 8x16 Unscii glyphs.
// ---------------------------------------------------------------------------

static void fill(int y0, int y1, uint16_t color) {
  volatile uint32_t *p = (volatile uint32_t*)&MEM_VRAM[y0 * 240];
  const uint32_t c2 = color | ((uint32_t)color << 16);
  for (int i = 0; i < (y1 - y0) * 240 / 2; i++)
    p[i] = c2;
}

static void text(int x, int y, const char *s, uint16_t color) {
  for (; *s && x + 8 <= 240; s++, x += 8) {
    const uint8_t c = (uint8_t)*s;
    const uint8_t *g = ts_font[(c >= 0x20 && c < 0x7F) ? c - 0x20 : '?' - 0x20];
    for (int r = 0; r < 16; r++) {
      volatile uint16_t *row = &MEM_VRAM[(y + r) * 240 + x];
      for (int b = 0; b < 8; b++)
        row[b] = (g[r] & (0x80 >> b)) ? color : COL_BG;
    }
  }
}

static void number(char *dst, unsigned v) {
  char tmp[11];
  unsigned n = 0;
  do {
    tmp[n++] = '0' + v % 10;
    v /= 10;
  } while (v);
  while (n)
    *dst++ = tmp[--n];
  *dst = 0;
}

static void status(const char *msg, uint16_t color) {
  fill(64, 112, COL_BG);
  text(120 - 4 * (int)strlen(msg), 72, msg, color);
}

static void bar(unsigned done, unsigned total) {
  const unsigned w = (unsigned)(((uint64_t)done * 200) / (total ? total : 1));
  volatile uint16_t *row = &MEM_VRAM[130 * 240 + 20];
  for (int y = 0; y < 6; y++)
    for (unsigned x = 0; x < 200; x++)
      row[y * 240 + x] = x < w ? COL_BAR : COL_DIM;
}

// Shows an error and waits for A to try again.
static void fail(const char *msg, unsigned code) {
  char line[32] = "Error ";
  number(&line[6], code);
  status(msg, COL_ERR);
  if (code)
    text(120 - 4 * (int)strlen(line), 92, line, COL_ERR);
  text(120 - 4 * 20, 140, "Press A to try again", COL_DIM);
  while (!(REG_KEYINPUT & KEY_BUTTA))
    ;
  while ((REG_KEYINPUT & KEY_BUTTA))
    ;
  // (the caller starts over)
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

// Starts the card, mounts it and loads the firmware into the SDRAM. Returns false
// if anything failed (after telling the user).
static bool load_firmware() {
  t_card_info info;

  status("Starting SD card...", COL_TEXT);
  set_supercard_mode(MAPPED_SDRAM, true, true);
  unsigned err = sdcard_init(&info);
  if (err) {
    fail("SD card not found", err);
    return false;
  }

  status("Reading SD card...", COL_TEXT);
  err = f_mount(&sdfs, "0:", 1);
  if (err) {
    fail("Can't read the SD card", err);
    return false;
  }

  FIL fd;
  err = f_open(&fd, FW_FILE, FA_READ);
  if (err) {
    fail("SFW.GBA not found", 0);
    return false;
  }

  const uint32_t size = f_size(&fd);
  if (size < 0x1000 || size > FW_MAX_SIZE) {
    f_close(&fd);
    fail("SFW.GBA has a bad size", 0);
    return false;
  }

  status("Loading SFW.GBA", COL_TEXT);
  uint32_t *buf = (uint32_t*)BUF_ADDR;
  volatile uint32_t *dst = (volatile uint32_t*)0x08000000;

  for (uint32_t done = 0; done < size; ) {
    UINT rd = 0;
    err = f_read(&fd, buf, BUF_SIZE, &rd);
    if (err || !rd) {
      f_close(&fd);
      fail("Read error", err);
      return false;
    }

    // The SDRAM is written with the SD interface off.
    set_supercard_mode(MAPPED_SDRAM, true, false);
    for (unsigned i = 0; i < (rd + 3) / 4; i++)
      dst[i] = buf[i];
    set_supercard_mode(MAPPED_SDRAM, true, true);
    dst += BUF_SIZE / 4;

    done += rd;
    bar(done, size);
  }
  f_close(&fd);

  // The first word of a GBA ROM is a branch (ARM, always).
  set_supercard_mode(MAPPED_SDRAM, true, false);
  const bool valid = (*(volatile uint32_t*)0x08000000 >> 24) == 0xEA;
  if (!valid) {
    set_supercard_mode(MAPPED_SDRAM, true, true);
    fail("SFW.GBA is not a GBA ROM", 0);
    return false;
  }
  return true;
}

int main() {
  REG_IME = 0;
  REG_WAITCNT = 0x40c0;            // Same setup as the firmware for the SD card

  REG_DISPCNT = 0x0403;            // Mode 3, BG2
  fill(0, 160, COL_BG);
  text(120 - 4 * 10, 16, "THUMBSTART", COL_TEXT);

  while (!load_firmware())
    ;

  // Boot it: ROM mapped to the SDRAM (read only, SD interface off), then a soft reset.
  status("Starting...", COL_TEXT);
  set_supercard_mode(MAPPED_SDRAM, false, false);
  REG_WAITCNT = 0x4000;
  launch_reset(false, false);
  return 0;
}
