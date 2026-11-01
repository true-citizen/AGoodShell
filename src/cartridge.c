#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "cartridge.h"
#include "compiler.h"
#include "gbahw.h"
#include "common.h"
#include "fatfs/ff.h"
#include "res/cartridge.h"

_Static_assert(CART_RLE_ART_X == CART_ART_X && CART_RLE_ART_Y == CART_ART_Y, "label window moved, update cartridge.h");
#define COLORS_FILE      "/ART/colors.txt"

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

#define LVL_R 5
#define LVL_G 6
#define LVL_B 3
_Static_assert(LVL_R * LVL_G * LVL_B == CART_PAL_ART_CNT && CART_PAL_ART + CART_PAL_ART_CNT <= 240, "box art palette");

void cart_init_palette() {
  for (unsigned b = 0; b < LVL_B; b++)
    for (unsigned g = 0; g < LVL_G; g++)
      for (unsigned r = 0; r < LVL_R; r++)
        MEM_PALETTE[CART_PAL_ART + r + LVL_R * (g + LVL_G * b)] =
          RGB2GBA(((r * 255 / (LVL_R - 1)) << 16) | ((g * 255 / (LVL_G - 1)) << 8) | (b * 255 / (LVL_B - 1)));
}

// Scales a color by num/256 and moves it towards white by white/256. The factors give
// the 3 shades of the picture (45, 105 and 28 for its gray) out of any base color.
static uint16_t shade(uint32_t rgb, unsigned num, unsigned white) {
  uint32_t out = 0;
  for (unsigned i = 0; i < 24; i += 8) {
    const unsigned c = (rgb >> i) & 0xFF;
    out |= (((c * num >> 8) + ((255 - c) * white >> 8)) & 0xFF) << i;
  }
  return RGB2GBA(out);
}

void cart_set_plastic(unsigned pal, uint32_t rgb) {
  MEM_PALETTE[pal]     = shade(rgb, 256, 0);
  MEM_PALETTE[pal + 1] = shade(rgb, 256, 73);
  MEM_PALETTE[pal + 2] = shade(rgb, 160, 0);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Decodes one row of the picture (starting at p, run length encoded) for a cartridge
// into the line buffer and returns a pointer to the next row.
static const uint8_t *decode_row(const uint8_t *p, unsigned y, const t_cartview *v, uint8_t *line) {
  int x = 0;
  while (x < CART_W) {
    const unsigned c = *p >> 5, n = (*p & 31) + 1;
    p++;
    if (c) {
      int x0 = x + v->dx, x1 = x0 + n, skip = 0;
      if (x0 < 0) {
        skip = -x0;
        x0 = 0;
      }
      if (x1 > CART_W)
        x1 = CART_W;
      if (x0 < x1) {
        if (c < 4)
          memset(&line[x0], v->pal + c - 1, x1 - x0);
        else if (v->art)
          memcpy(&line[x0], &v->art[(y - CART_ART_Y) * CART_ART_W + x + skip - CART_ART_X], x1 - x0);
        else
          memset(&line[x0], v->fill, x1 - x0);
      }
    }
    x += n;
  }
  return p;
}

void cart_render(volatile uint8_t *rows, const t_cartview *views, unsigned count, uint8_t bg) {
  const uint8_t *next[2], *last[2];     // Next row to read and the last one decoded
  uint16_t line16[CART_W / 2];
  uint8_t *line = (uint8_t*)line16;
  for (unsigned i = 0; i < count; i++)
    next[i] = last[i] = cart_rle;

  for (unsigned y = 0; y < CART_H; y++) {
    memset(line, bg, CART_W);
    for (unsigned i = 0; i < count; i++) {
      if (*next[i] == 0xFF) {
        next[i]++;
        decode_row(last[i], y, &views[i], line);
      } else {
        last[i] = next[i];
        next[i] = decode_row(next[i], y, &views[i], line);
      }
    }
    dma_memcpy16(&rows[y * CART_W], line16, CART_W / 2);
  }
}

// ---------------------------------------------------------------------------
// Box art (BMP) loader
// ---------------------------------------------------------------------------

static uint32_t le(const uint8_t *p, unsigned n) {
  uint32_t v = 0;
  while (n--)
    v = (v << 8) | p[n];
  return v;
}

// Ordered dither thresholds (4x4 Bayer matrix, scaled to 8..248)
static const uint8_t bayer[16] = { 8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88 };

bool cart_load_art(const char *path, uint8_t *dst) {
  FIL fd;
  if (FR_OK != f_open(&fd, path, FA_READ))
    return false;

  uint8_t hdr[54], row[CART_ART_W * 4];
  uint32_t pal[256];
  UINT rd;
  bool ok = false;

  do {
    if (FR_OK != f_read(&fd, hdr, sizeof(hdr), &rd) || rd != sizeof(hdr) || hdr[0] != 'B' || hdr[1] != 'M')
      break;
    const uint32_t dataoff = le(&hdr[10], 4), dibsize = le(&hdr[14], 4);
    const int32_t w = le(&hdr[18], 4), h = le(&hdr[22], 4);
    const unsigned bpp = le(&hdr[28], 2);
    if (dibsize < 40 || le(&hdr[30], 4) != 0 || w != CART_ART_W || (h != CART_ART_H && h != -CART_ART_H))
      break;
    if (bpp != 8 && bpp != 24 && bpp != 32)
      break;

    if (bpp == 8) {
      // Palette entries are B, G, R, 0.
      unsigned colors = le(&hdr[46], 4);
      if (!colors || colors > 256)
        colors = 256;
      if (FR_OK != f_lseek(&fd, 14 + dibsize) || FR_OK != f_read(&fd, pal, colors * 4, &rd) || rd != colors * 4)
        break;
    }

    const unsigned stride = (CART_ART_W * bpp / 8 + 3) & ~3U;
    if (FR_OK != f_lseek(&fd, dataoff))
      break;

    ok = true;
    for (unsigned i = 0; i < CART_ART_H && ok; i++) {
      // Rows are stored bottom to top unless the height is negative.
      const unsigned y = h > 0 ? CART_ART_H - 1 - i : i;
      if (FR_OK != f_read(&fd, row, stride, &rd) || rd != stride) {
        ok = false;
        break;
      }
      uint8_t out[CART_ART_W];
      for (unsigned x = 0; x < CART_ART_W; x++) {
        unsigned r, g, b;
        if (bpp == 8) {
          const uint32_t c = pal[row[x]];
          b = c & 0xFF; g = (c >> 8) & 0xFF; r = (c >> 16) & 0xFF;
        } else {
          const uint8_t *px = &row[x * (bpp / 8)];
          b = px[0]; g = px[1]; r = px[2];
        }
        // Reduce to the palette. Colors that are in the palette stay as they are.
        const unsigned th = bayer[(y & 3) * 4 + (x & 3)];
        r = (r * (LVL_R - 1) + th) / 255;
        g = (g * (LVL_G - 1) + th) / 255;
        b = (b * (LVL_B - 1) + th) / 255;
        out[x] = CART_PAL_ART + r + LVL_R * (g + LVL_G * b);
      }
      dma_memcpy16(&dst[y * CART_ART_W], out, CART_ART_W / 2);
    }
  } while (0);

  f_close(&fd);
  return ok;
}

bool cart_read_gcode(const char *path, char *gcode) {
  FIL fd;
  UINT rd;
  if (FR_OK != f_open(&fd, path, FA_READ))
    return false;
  // The game code is 4 bytes at 0xAC of the ROM header
  bool ok = FR_OK == f_lseek(&fd, 0xAC) && FR_OK == f_read(&fd, gcode, 4, &rd) && rd == 4;
  f_close(&fd);
  gcode[4] = 0;
  for (unsigned i = 0; i < 4 && ok; i++)
    ok = (gcode[i] >= '0' && gcode[i] <= '9') || (gcode[i] >= 'A' && gcode[i] <= 'Z');
  return ok;
}

// ---------------------------------------------------------------------------
// Plastic colors
// ---------------------------------------------------------------------------

static const struct { const char *name; uint32_t rgb; } color_names[] = {
  { "black",  0x2d2d2d }, { "gray",   0x8e8e98 }, { "white",  0xe8e8e8 }, { "red",    0xc82828 },
  { "orange", 0xe08020 }, { "yellow", 0xe8c820 }, { "gold",   0xc8a830 }, { "green",  0x28a040 },
  { "teal",   0x20a0a0 }, { "blue",   0x2850c8 }, { "purple", 0x8030b0 }, { "pink",   0xe060a0 },
};

// Parses a color: one of the names above or 6 hex digits (with an optional #).
static bool parse_color(const char *s, uint32_t *rgb) {
  if (*s == '#')
    s++;
  uint32_t v = 0;
  unsigned i = 0;
  for (; i < 6; i++) {
    const char c = s[i];
    const unsigned d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 16;
    if (d > 15)
      break;
    v = (v << 4) | d;
  }
  if (i == 6 && !s[6]) {
    *rgb = v;
    return true;
  }
  for (i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++) {
    if (!strcasecmp(s, color_names[i].name)) {
      *rgb = color_names[i].rgb;
      return true;
    }
  }
  return false;
}

// Trims spaces and carriage returns at both ends (in place) and returns the start.
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = &s[strlen(s)];
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
    *--e = 0;
  return s;
}

// Handles one "id=color" line. A match with the file name wins over one with the game code.
// Returns true when the name matched.
static bool color_line(char *line, const char *name, const char *gcode, uint32_t *rgb, bool *found) {
  char *eq = strrchr(line, '=');
  uint32_t c;
  if (!eq || line[0] == '#')
    return false;
  *eq = 0;
  char *id = trim(line);
  if (!parse_color(trim(eq + 1), &c))
    return false;
  if (!strcasecmp(id, name)) {
    *rgb = c;
    *found = true;
    return true;
  }
  if (gcode && !*found && !strcasecmp(id, gcode)) {
    *rgb = c;
    *found = true;
  }
  return false;
}

bool cart_find_color(const char *name, const char *gcode, uint32_t *rgb) {
  FIL fd;
  if (FR_OK != f_open(&fd, COLORS_FILE, FA_READ))
    return false;

  bool found = false, byname = false, skip = false;
  char line[64], buf[128];
  unsigned len = 0;
  UINT rd = 0;

  do {
    if (FR_OK != f_read(&fd, buf, sizeof(buf), &rd))
      break;
    for (unsigned i = 0; i < rd && !byname; i++) {
      if (buf[i] != '\n') {
        if (len < sizeof(line) - 1)
          line[len++] = buf[i];
        else
          skip = true;      // Line too long, ignore it
      } else {
        line[len] = 0;
        if (!skip)
          byname = color_line(line, name, gcode, rgb, &found);
        len = 0;
        skip = false;
      }
    }
  } while (rd == sizeof(buf) && !byname);

  // Last line without a newline at the end
  if (!byname && !skip && len) {
    line[len] = 0;
    color_line(line, name, gcode, rgb, &found);
  }
  f_close(&fd);
  return found;
}
