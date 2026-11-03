/*
 * GIF decoder. Pictures up to 240x160 are composited in a canvas and animated (shown with an
 * integer zoom if they are smaller than the screen); bigger ones show their first frame only,
 * scaled down by picking pixels. Disposal "restore previous" is treated as "leave".
 */
#include <string.h>
#include "image.h"

static const uint8_t *g, *gend;
static uint16_t gpal[256];                        // Global palette, RGB555
static uint16_t lpal[256];                        // Palette of the frame
static unsigned canvas_w, canvas_h;

// --- LZW ---------------------------------------------------------------------------------

static const uint8_t *bp;                         // Position in the data sub-blocks
static unsigned blk_left;
static uint32_t lbits;
static unsigned lcount;

static int lzw_byte(void) {
  while (!blk_left) {
    if (bp >= gend) return -1;
    blk_left = *bp++;
    if (!blk_left) return -1;                     // Terminator
  }
  if (bp >= gend) return -1;
  blk_left--;
  return *bp++;
}

typedef void (*row_fn)(unsigned y, const uint8_t *idx);

static uint16_t *prefix;
static uint8_t *suffix, *stack;

static const char *lzw_frame(unsigned fw, unsigned fh, bool interlace, row_fn fn, uint8_t *rowbuf) {
  const int mcs = *bp++;
  if (mcs < 2 || mcs > 11) return "Bad GIF data";
  const unsigned clear = 1u << mcs, eoi = clear + 1;
  unsigned next = clear + 2, size = mcs + 1, x = 0, row = 0;
  int old = -1, first = 0;
  blk_left = 0; lbits = 0; lcount = 0;
  static const uint8_t pstart[4] = {0, 4, 2, 1}, pstep[4] = {8, 8, 4, 2};
  unsigned pass = 0, ry = 0;

  for (;;) {
    while (lcount < size) {
      const int b = lzw_byte();
      if (b < 0) goto end;
      lbits |= (uint32_t)b << lcount; lcount += 8;
    }
    unsigned code = lbits & ((1u << size) - 1);
    lbits >>= size; lcount -= size;
    if (code == eoi) break;
    if (code == clear) { size = mcs + 1; next = clear + 2; old = -1; continue; }
    unsigned sp = 0;
    if (old < 0) {
      if (code >= clear) goto end;
      stack[sp++] = first = code; old = code;
    } else {
      const unsigned incode = code;
      if (code > next) goto end;
      if (code == next) { stack[sp++] = first; code = old; }
      while (code >= clear) { stack[sp++] = suffix[code]; code = prefix[code]; }
      first = code;
      stack[sp++] = first;
      if (next < 4096) {
        prefix[next] = old; suffix[next] = first; next++;
        if (next == (1u << size) && size < 12) size++;
      }
      old = incode;
    }
    while (sp) {
      rowbuf[x++] = stack[--sp];
      if (x == fw) {
        x = 0;
        fn(interlace ? ry : row, rowbuf);
        row++;
        if (interlace) {
          ry += pstep[pass];
          while (ry >= fh && pass < 3) { pass++; ry = pstart[pass]; }
        }
        if (row >= fh) goto end;
      }
    }
  }
end:
  // Skip what is left of the data
  while (lzw_byte() >= 0) ;
  return NULL;
}

// --- Frames ------------------------------------------------------------------------------

static uint16_t *canvas;
static unsigned fx, fy, fw, fh;
static int trans;
static const uint16_t *fpal;

static void row_canvas(unsigned y, const uint8_t *idx) {
  const unsigned cy = fy + y;
  if (cy >= canvas_h) return;
  for (unsigned x = 0; x < fw && fx + x < canvas_w; x++)
    if ((int)idx[x] != trans)
      canvas[cy * canvas_w + fx + x] = fpal[idx[x]];
}

static void row_near(unsigned y, const uint8_t *idx) {
  const int dy = sink_near_dy(fy + y);
  if (dy < 0) return;
  volatile uint16_t *o = &VRAM[dy * 240 + sink_ox];
  for (unsigned dx = 0; dx < sink_dw; dx++) {
    const int sx = (int)sink_dxmap[dx] - (int)fx;
    if (sx >= 0 && sx < (int)fw && (int)idx[sx] != trans)
      o[dx] = fpal[idx[sx]];
  }
}

static void read_palette(uint16_t *pal, const uint8_t *p, unsigned n) {
  for (unsigned i = 0; i < n; i++, p += 3)
    pal[i] = RGB555(p[0], p[1], p[2]);
}

static void show_canvas(unsigned k, unsigned ox, unsigned oy) {
  for (unsigned y = 0; y < canvas_h; y++)
    for (unsigned ky = 0; ky < k; ky++) {
      volatile uint16_t *o = &VRAM[(oy + y * k + ky) * 240 + ox];
      const uint16_t *s = &canvas[y * canvas_w];
      if (k == 1) for (unsigned x = 0; x < canvas_w; x++) o[x] = s[x];
      else for (unsigned x = 0; x < canvas_w; x++)
        for (unsigned kx = 0; kx < k; kx++) o[x * k + kx] = s[x];
    }
}

const char *gif_show(const uint8_t *d, unsigned n) {
  g = d; gend = d + n;
  if (n < 13) return "Broken GIF";
  canvas_w = d[6] | (d[7] << 8); canvas_h = d[8] | (d[9] << 8);
  const unsigned flags = d[10];
  const uint8_t *p = d + 13;
  if (!canvas_w || !canvas_h || canvas_w > MAX_IMG_W || canvas_h > MAX_IMG_H) return "Picture too big";
  if (flags & 0x80) {
    const unsigned cn = 2u << (flags & 7);
    if (p + 3 * cn > gend) return "Broken GIF";
    read_palette(gpal, p, cn);
    p += 3 * cn;
  }
  const uint8_t *const first_block = p;

  // Scratch: LZW tables, one row of indices, and the canvas of small pictures
  prefix = (uint16_t*)scratch;
  suffix = scratch + 8192;
  stack = suffix + 4096 + 4;
  uint8_t *rowbuf = stack + 4096 + 4;
  canvas = (uint16_t*)(((uintptr_t)(rowbuf + MAX_IMG_W) + 3) & ~3);

  const bool small = canvas_w <= 240 && canvas_h <= 160;
  unsigned k = 1, ox = 0, oy = 0;
  if (small) {
    while (canvas_w * (k + 1) <= 240 && canvas_h * (k + 1) <= 160) k++;
    ox = (240 - canvas_w * k) / 2; oy = (160 - canvas_h * k) / 2;
    memset(canvas, 0, canvas_w * canvas_h * 2);
    sink_begin(canvas_w, canvas_h, true);         // Only clears the screen and sets the mode
  } else {
    sink_begin(canvas_w, canvas_h, true);
  }

  unsigned delay = 10, frames = 0, prev_disp = 0, px = 0, py = 0, pw = 0, ph = 0;
  int tr = -1;
  unsigned disp = 0;
  for (;;) {
    if (p >= gend) goto rewind;
    const unsigned t = *p++;
    if (t == 0x3B) goto rewind;                   // Trailer
    if (t == 0x21) {                              // Extension
      const unsigned label = *p++;
      if (label == 0xF9 && p[0] >= 4) {           // Graphic control: disposal, delay, transparency
        disp = (p[1] >> 2) & 7;
        delay = p[2] | (p[3] << 8);
        tr = (p[1] & 1) ? (int)p[4] : -1;
      }
      while (p < gend && *p) p += *p + 1;         // Sub-blocks
      p++;
      continue;
    }
    if (t != 0x2C || p + 9 > gend) goto rewind;
    fx = p[0] | (p[1] << 8); fy = p[2] | (p[3] << 8);
    fw = p[4] | (p[5] << 8); fh = p[6] | (p[7] << 8);
    const unsigned fl = p[8];
    p += 9;
    if (fl & 0x80) {
      const unsigned cn = 2u << (fl & 7);
      if (p + 3 * cn > gend) return frames ? NULL : "Broken GIF";
      read_palette(lpal, p, cn);
      fpal = lpal; p += 3 * cn;
    } else fpal = gpal;
    if (!fw || !fh || fw > MAX_IMG_W) return frames ? NULL : "Broken GIF";
    trans = tr;
    bp = p;
    if (small) {
      if (prev_disp == 2)                         // Restore to background: the area of the last frame
        for (unsigned y = py; y < py + ph && y < canvas_h; y++)
          for (unsigned x = px; x < px + pw && x < canvas_w; x++) canvas[y * canvas_w + x] = 0;
      const char *e = lzw_frame(fw, fh, fl & 0x40, row_canvas, rowbuf);
      if (e) return frames ? NULL : e;
      show_canvas(k, ox, oy);
    } else {
      const char *e = lzw_frame(fw, fh, fl & 0x40, row_near, rowbuf);
      if (e) return frames ? NULL : e;
      return NULL;                                // First frame only
    }
    p = bp;
    frames++;
    prev_disp = disp; px = fx; py = fy; pw = fw; ph = fh;
    tr = -1; disp = 0;
    unsigned wait = delay * 60 / 100;
    if (wait < 2) wait = 2;
    while (wait--) { vsync(); key_b(); }
    delay = 10;
    continue;

rewind:
    if (frames <= 1) return frames ? NULL : "No picture in GIF";
    p = first_block;                              // Loop
    memset(canvas, 0, canvas_w * canvas_h * 2);
    prev_disp = 0;
  }
}
