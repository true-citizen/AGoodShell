/*
 * PNG decoder: a small inflate (fast table for the short codes) that feeds scanlines to the
 * sink as they come out. All color types and bit depths, not interlaced (Adam7), alpha is
 * blended over black.
 */
#include <string.h>
#include "image.h"

#define WIN 32768
#define MAXROW 16385                              // Filter byte + the bytes of the widest row we take

static uint8_t *win, *rowa, *rowb, *rgb;          // Pointers into the scratch memory
static uint8_t *cur, *prev;
static unsigned wp, rp, rowlen, bpp, img_w, img_h, depth, ctype, row_y;
static uint8_t pal[256][4];

// Input: the IDAT chunks one after another
static const uint8_t *ip, *iend;
static unsigned chunk_left;
static bool ierr;

static unsigned be32(const uint8_t *p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static void next_chunk(void) {
  for (;;) {
    ip += 4;                                      // CRC of the last one
    if (ip + 8 > iend) { chunk_left = 0; ierr = true; return; }
    const unsigned len = be32(ip);
    const bool idat = !memcmp(ip + 4, "IDAT", 4);
    ip += 8;
    if (idat) { chunk_left = len; if (len) return; continue; }
    ierr = true; chunk_left = 0; return;          // No more compressed data
  }
}

static unsigned nextbyte(void) {
  while (!chunk_left) {
    if (ierr) return 0;
    next_chunk();
  }
  if (ip >= iend) { ierr = true; return 0; }
  chunk_left--;
  return *ip++;
}

static uint32_t bb;
static unsigned bc;
static inline void need(unsigned n) { while (bc < n) { bb |= nextbyte() << bc; bc += 8; } }
static inline unsigned getbits(unsigned n) {
  if (!n) return 0;
  need(n);
  unsigned v = bb & ((1u << n) - 1);
  bb >>= n; bc -= n;
  return v;
}

typedef struct {
  uint16_t fast[512];                             // (symbol << 4) | length for codes up to 9 bits
  uint16_t count[16];
  uint16_t sym[288];
} t_huff;
static t_huff lit, dist;

static void build(t_huff *h, const uint8_t *len, unsigned n) {
  uint16_t offs[16];
  memset(h->count, 0, sizeof(h->count));
  for (unsigned i = 0; i < n; i++) h->count[len[i]]++;
  h->count[0] = 0;
  offs[1] = 0;
  for (unsigned l = 1; l < 15; l++) offs[l + 1] = offs[l] + h->count[l];
  for (unsigned i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = i;
  memset(h->fast, 0, sizeof(h->fast));
  unsigned code = 0, idx = 0;
  for (unsigned l = 1; l <= 15; l++) {
    for (unsigned k = 0; k < h->count[l]; k++, idx++, code++) {
      if (l > 9) continue;
      unsigned rev = 0;
      for (unsigned b = 0; b < l; b++) if (code & (1u << b)) rev |= 1u << (l - 1 - b);
      for (unsigned i = rev; i < 512; i += 1u << l) h->fast[i] = (h->sym[idx] << 4) | l;
    }
    code <<= 1;
  }
}

static int decode(const t_huff *h) {
  need(9);
  const unsigned f = h->fast[bb & 511];
  if (f) { const unsigned l = f & 15; bb >>= l; bc -= l; return f >> 4; }
  int code = 0, first = 0, index = 0;             // The long codes, bit by bit
  for (unsigned l = 1; l <= 15; l++) {
    code |= getbits(1);
    const int c = h->count[l];
    if (code - c < first) return h->sym[index + (code - first)];
    index += c; first += c; first <<= 1; code <<= 1;
  }
  return -1;
}

// --- Scanlines ---------------------------------------------------------------

static void emit_row(void) {
  uint8_t *r = cur + 1;
  const unsigned f = cur[0], stride = rowlen - 1;
  switch (f) {
  case 1: for (unsigned i = bpp; i < stride; i++) r[i] += r[i - bpp]; break;
  case 2: for (unsigned i = 0; i < stride; i++) r[i] += prev[1 + i]; break;
  case 3:
    for (unsigned i = 0; i < stride; i++)
      r[i] += ((i >= bpp ? r[i - bpp] : 0) + prev[1 + i]) >> 1;
    break;
  case 4:
    for (unsigned i = 0; i < stride; i++) {
      const int a = i >= bpp ? r[i - bpp] : 0, b = prev[1 + i], c = i >= bpp ? prev[1 + i - bpp] : 0;
      const int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
      r[i] += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
    }
    break;
  }
  uint8_t *o = rgb;
  switch (ctype) {
  case 0: case 4: {                               // Gray (+ alpha)
    const unsigned step = ctype == 4 ? (depth == 16 ? 4 : 2) : 0;
    for (unsigned x = 0; x < img_w; x++, o += 3) {
      unsigned v, a = 255;
      if (ctype == 4) { v = r[x * step]; a = r[x * step + (depth == 16 ? 2 : 1)]; }
      else if (depth == 16) v = r[x * 2];
      else if (depth == 8) v = r[x];
      else {
        const unsigned bit = x * depth;
        v = (r[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
        v = depth == 1 ? v * 255 : depth == 2 ? v * 85 : v * 17;
      }
      v = (v * (a + 1)) >> 8;
      o[0] = o[1] = o[2] = v;
    }
    break; }
  case 2: case 6: {                               // RGB (+ alpha)
    const unsigned px = (ctype == 6 ? 4 : 3) * (depth == 16 ? 2 : 1), cs = depth == 16 ? 2 : 1;
    for (unsigned x = 0; x < img_w; x++, o += 3) {
      const uint8_t *s = r + x * px;
      unsigned a = ctype == 6 ? s[3 * cs] : 255;
      o[0] = (s[0] * (a + 1)) >> 8; o[1] = (s[cs] * (a + 1)) >> 8; o[2] = (s[2 * cs] * (a + 1)) >> 8;
    }
    break; }
  case 3:
    for (unsigned x = 0; x < img_w; x++, o += 3) {
      unsigned i;
      if (depth == 8) i = r[x];
      else { const unsigned bit = x * depth; i = (r[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1); }
      const uint8_t *p = pal[i];
      o[0] = (p[0] * (p[3] + 1)) >> 8; o[1] = (p[1] * (p[3] + 1)) >> 8; o[2] = (p[2] * (p[3] + 1)) >> 8;
    }
    break;
  }
  sink_span(row_y, 0, img_w, rgb);
  row_y++;
  sink_rows_done(row_y);
  uint8_t *t = cur; cur = prev; prev = t;
  rp = 0;
}

static inline void out(unsigned c) {
  win[wp++ & (WIN - 1)] = c;
  if (row_y >= img_h) return;
  cur[rp++] = c;
  if (rp == rowlen) emit_row();
}

// --- Inflate -----------------------------------------------------------------

static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static const char *inflate_all(void) {
  nextbyte(); nextbyte();                         // zlib header
  unsigned last;
  do {
    last = getbits(1);
    const unsigned type = getbits(2);
    if (type == 0) {
      bb >>= bc & 7; bc -= bc & 7;
      const unsigned len = getbits(16); getbits(16);
      for (unsigned i = 0; i < len && !ierr; i++) out(getbits(8));
      if (ierr && row_y < img_h) return "Truncated file";
      continue;
    }
    if (type == 3) return "Bad PNG data";
    uint8_t lens[320];
    if (type == 1) {
      for (unsigned i = 0; i < 288; i++) lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
      build(&lit, lens, 288);
      for (unsigned i = 0; i < 30; i++) lens[i] = 5;
      build(&dist, lens, 30);
    } else {
      static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
      const unsigned nl = getbits(5) + 257, nd = getbits(5) + 1, nc = getbits(4) + 4;
      t_huff cl;
      if (nl > 286 || nd > 30) return "Bad PNG data";
      memset(lens, 0, 19);
      for (unsigned i = 0; i < nc; i++) lens[order[i]] = getbits(3);
      build(&cl, lens, 19);
      uint8_t ll[320];
      unsigned i = 0;
      while (i < nl + nd) {
        int s = decode(&cl);
        if (s < 0 || ierr) return "Bad PNG data";
        if (s < 16) ll[i++] = s;
        else {
          unsigned rep, v = 0;
          if (s == 16) { if (!i) return "Bad PNG data"; v = ll[i - 1]; rep = 3 + getbits(2); }
          else if (s == 17) rep = 3 + getbits(3);
          else rep = 11 + getbits(7);
          if (i + rep > nl + nd) return "Bad PNG data";
          while (rep--) ll[i++] = v;
        }
      }
      build(&lit, ll, nl);
      build(&dist, ll + nl, nd);
    }
    for (;;) {
      int s = decode(&lit);
      if (s < 0 || ierr) return row_y >= img_h ? NULL : "Truncated file";
      if (s < 256) { out(s); continue; }
      if (s == 256) break;
      s -= 257;
      if (s >= 29) return "Bad PNG data";
      const unsigned len = lbase[s] + getbits(lext[s]);
      const int ds = decode(&dist);
      if (ds < 0 || ds >= 30) return "Bad PNG data";
      const unsigned d = dbase[ds] + getbits(dext[ds]);
      if (d > wp) return "Bad PNG data";
      for (unsigned i = 0; i < len; i++) out(win[(wp - d) & (WIN - 1)]);
    }
    if (row_y >= img_h) return NULL;
  } while (!last);
  return row_y >= img_h ? NULL : "Truncated file";
}

const char *png_show(const uint8_t *d, unsigned n) {
  const uint8_t *p = d + 8, *idat = NULL;
  iend = d + n;
  uint8_t ihdr[13];
  bool have_hdr = false;
  memset(pal, 0, sizeof(pal));
  for (unsigned i = 0; i < 256; i++) pal[i][3] = 255;
  while (p + 12 <= iend) {
    const unsigned len = be32(p);
    const uint8_t *t = p + 4, *c = p + 8;
    if (!memcmp(t, "IHDR", 4) && len >= 13) { memcpy(ihdr, c, 13); have_hdr = true; }
    else if (!memcmp(t, "PLTE", 4)) { for (unsigned i = 0; i < len / 3 && i < 256; i++) memcpy(pal[i], c + 3 * i, 3); }
    else if (!memcmp(t, "tRNS", 4)) { for (unsigned i = 0; i < len && i < 256; i++) pal[i][3] = c[i]; }
    else if (!memcmp(t, "IDAT", 4)) { idat = p; break; }
    p += 12 + len;
  }
  if (!have_hdr || !idat) return "Broken PNG";
  img_w = be32(ihdr); img_h = be32(ihdr + 4);
  depth = ihdr[8]; ctype = ihdr[9];
  if (ihdr[12]) return "Interlaced PNG unsupported";
  if (!img_w || !img_h || img_w > MAX_IMG_W || img_h > MAX_IMG_H) return "Picture too big";
  static const uint8_t chans[7] = {1, 0, 3, 1, 2, 0, 4};
  if (ctype > 6 || !chans[ctype]) return "Unknown PNG color type";
  const unsigned bits = chans[ctype] * depth;
  rowlen = 1 + (img_w * bits + 7) / 8;
  bpp = bits >= 8 ? bits / 8 : 1;
  if (rowlen > MAXROW) return "Picture too wide";

  win = scratch; rowa = win + WIN; rowb = rowa + MAXROW; rgb = rowb + MAXROW;
  memset(rowa, 0, MAXROW); memset(rowb, 0, MAXROW);
  cur = rowa; prev = rowb;
  wp = rp = row_y = 0; bb = bc = 0; ierr = false;
  ip = idat + 8; chunk_left = be32(idat);
  sink_begin(img_w, img_h, false);
  const char *err = inflate_all();
  if (err && row_y == 0) return err;
  sink_rows_done(img_h);                          // What there is of a damaged picture
  return NULL;
}
