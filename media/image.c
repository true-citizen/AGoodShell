/*
 * Picture viewer (.png, .jpg, .gif) of the media ROM. The file is in the ROM area, after the
 * header the firmware writes; the decoders read it from there and draw rows into the mode 3
 * screen as they go. Big pictures are scaled down to fit; pictures that fit are shown 1:1.
 */
#include <string.h>
#include "image.h"

uint8_t scratch[SCRATCH_SIZE] __attribute__((aligned(4)));

#define MAXBAND 20                                // Screen rows that can be in progress (a JPEG MCU row has up to 16 source rows)

void image_run(const t_media_header *h);
static unsigned sw, sh, dw, dh;
static bool ident, nearest;
unsigned sink_ox, sink_oy, sink_dw;
uint16_t sink_dxmap[MAX_IMG_W];
static uint8_t colcnt[240];
static uint32_t acc[MAXBAND][240][3];
static unsigned flushed;

void vsync(void) {
  while (REG16(0x04000006) >= 160) ;
  while (REG16(0x04000006) < 160) ;
}

bool key_b(void) {
  if (~REG16(0x04000130) & 0x02)
    exit_player();
  return false;
}

void fit_dims(unsigned w, unsigned h, unsigned *ow, unsigned *oh) {
  if (w <= 240 && h <= 160) { *ow = w; *oh = h; return; }
  if ((uint32_t)w * 160 > (uint32_t)h * 240) { *ow = 240; *oh = (uint32_t)h * 240 / w; }
  else { *oh = 160; *ow = (uint32_t)w * 160 / h; }
  if (!*ow) *ow = 1;
  if (!*oh) *oh = 1;
}

void sink_begin(unsigned w, unsigned h, bool near) {
  sw = w; sh = h; nearest = near;
  fit_dims(w, h, &dw, &dh);
  ident = (dw == w && dh == h);
  sink_ox = (240 - dw) / 2; sink_oy = (160 - dh) / 2; sink_dw = dw;
  REG16(0x04000000) = 0x0403;
  rect(0, 0, 240, 160, 0);
  memset(acc, 0, sizeof(acc));
  memset(colcnt, 0, sizeof(colcnt));
  flushed = 0;
  if (ident) return;
  if (near) {
    for (unsigned dx = 0; dx < dw; dx++) sink_dxmap[dx] = (uint32_t)dx * w / dw;
  } else {
    for (unsigned x = 0; x < w; x++) { unsigned dx = (uint32_t)x * dw / w; sink_dxmap[x] = dx; colcnt[dx]++; }
  }
}

int sink_near_dy(unsigned y) {
  if (ident) return y < sh ? (int)(sink_oy + y) : -1;
  unsigned dy = ((uint32_t)y * dh + sh - 1) / sh;
  if (dy < dh && (uint32_t)dy * sh / dh == y) return sink_oy + dy;
  return -1;
}

void sink_span(unsigned y, unsigned x0, unsigned n, const uint8_t *rgb) {
  if (y >= sh || x0 >= sw) return;
  if (x0 + n > sw) n = sw - x0;
  if (ident) {
    volatile uint16_t *d = &VRAM[(sink_oy + y) * 240 + sink_ox + x0];
    for (unsigned i = 0; i < n; i++, rgb += 3)
      d[i] = RGB555(rgb[0], rgb[1], rgb[2]);
    return;
  }
  const unsigned dy = (uint32_t)y * dh / sh;
  uint32_t (*row)[3] = acc[dy % MAXBAND];
  for (unsigned i = 0; i < n; i++, rgb += 3) {
    uint32_t *a = row[sink_dxmap[x0 + i]];
    a[0] += rgb[0]; a[1] += rgb[1]; a[2] += rgb[2];
  }
}

static void flush_row(unsigned d) {
  const unsigned rows = ((d + 1) * sh + dh - 1) / dh - (d * sh + dh - 1) / dh;
  uint32_t (*row)[3] = acc[d % MAXBAND];
  volatile uint16_t *o = &VRAM[(sink_oy + d) * 240 + sink_ox];
  for (unsigned x = 0; x < dw; x++) {
    const uint32_t inv = 65536 / (colcnt[x] * rows ? colcnt[x] * rows : 1);
    o[x] = RGB555((row[x][0] * inv) >> 16, (row[x][1] * inv) >> 16, (row[x][2] * inv) >> 16);
    row[x][0] = row[x][1] = row[x][2] = 0;
  }
}

void sink_rows_done(unsigned y) {
  key_b();
  if (ident || nearest) return;
  const unsigned lim = y >= sh ? dh : (uint32_t)y * dh / sh;
  while (flushed < lim)
    flush_row(flushed++);
}

extern uint32_t __iwi_lma[], __iwi_start[], __iwi_end[];

__attribute__((section(".ewram.boot"), noinline))
void image_main(const t_media_header *h) {
  // The viewer's hot code goes to the IWRAM (over the MP3 decoder)
  REG16(0x04000000) = 0x0080;
  for (uint32_t *s = __iwi_lma, *d = __iwi_start; d < __iwi_end; )
    *d++ = *s++;
  image_run(h);
}

void image_run(const t_media_header *h) {
  const uint8_t *d = (const uint8_t*)&h[1];
  const unsigned n = h->size;
  const char *err;
  REG16(0x04000000) = 0x0080;
  if (n > 8 && !memcmp(d, "\x89PNG", 4))
    err = png_show(d, n);
  else if (n > 4 && d[0] == 0xFF && d[1] == 0xD8)
    err = jpg_show(d, n);
  else if (n > 6 && !memcmp(d, "GIF8", 4))
    err = gif_show(d, n);
  else
    err = "Not a PNG, JPEG or GIF file";

  if (err) {
    REG16(0x04000000) = 0x0403;
    theme_init(h);
    bg_paint(0, 0, 240, 160);
    text(8, 8, "Cannot show picture", C_WARN);
    char name[33];
    memcpy(name, h->title, 32); name[32] = 0;
    text(8, 32, name, C_FG);
    text(8, 56, err, C_DIM);
    text(8, 144, "B:Exit", C_DIM);
  }
  for (;;) {                                  // Done (or an animation that ended): B leaves
    vsync();
    key_b();
  }
}
