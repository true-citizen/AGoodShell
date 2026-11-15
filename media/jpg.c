/*
 * JPEG viewer on top of picojpeg (third_party/picojpeg): baseline JPEG only, no progressive.
 * It decodes MCU by MCU and feeds the sink. Pictures at least 8 times bigger than the screen
 * use picojpeg's "reduce" mode, which gets one pixel per 8x8 block and is much faster.
 */
#include <string.h>
#include "image.h"
#include "../third_party/picojpeg/picojpeg.h"

static const uint8_t *jd;
static unsigned jn, jpos;

static unsigned char need_bytes(unsigned char *buf, unsigned char size, unsigned char *got, void *user) {
  unsigned n = jn - jpos;
  if (n > size) n = size;
  memcpy(buf, jd + jpos, n);
  jpos += n;
  *got = n;
  return 0;
}

const char *jpg_show(const uint8_t *d, unsigned n) {
  pjpeg_image_info_t info;
  jd = d; jn = n; jpos = 0;
  unsigned w, h, dw, dh;
  // The first pass only reads the size, to choose between normal and reduced decoding
  unsigned char st = pjpeg_decode_init(&info, need_bytes, NULL, 0);
  if (st == PJPG_UNSUPPORTED_MODE) return "Progressive JPEG unsupported";
  if (st) return "Broken JPEG";
  w = info.m_width; h = info.m_height;
  if (!w || !h || w > 65535 || h > 65535) return "Broken JPEG";
  fit_dims(w, h, &dw, &dh);
  const bool reduce = w / 8 >= dw && h / 8 >= dh && (w > 240 || h > 160);
  if (reduce) {
    jpos = 0;
    st = pjpeg_decode_init(&info, need_bytes, NULL, 1);
    if (st) return "Broken JPEG";
  }
  const unsigned mw = info.m_MCUWidth, mh = info.m_MCUHeight;
  const unsigned bw = mw >> 3, bh = mh >> 3;
  const bool gray = info.m_scanType == PJPG_GRAYSCALE;
  unsigned ow = reduce ? (w + 7) / 8 : w, oh = reduce ? (h + 7) / 8 : h;
  if (ow > MAX_IMG_W) return "Picture too wide";
  sink_begin(ow, oh, false);

  uint8_t *tmp = scratch;                         // One row of an MCU
  for (unsigned my = 0, mx = 0;;) {
    st = pjpeg_decode_mcu();
    if (st == PJPG_NO_MORE_BLOCKS) break;
    if (st) { if (my) break; return "Broken JPEG"; }
    if (reduce) {
      for (unsigned by = 0; by < bh; by++) {
        const unsigned y = my * bh + by;
        if (y >= oh) break;
        unsigned cnt = 0;
        for (unsigned bx = 0; bx < bw; bx++) {
          if (mx * bw + bx >= ow) break;
          const unsigned o = bx * 64 + by * 128;
          tmp[cnt * 3] = info.m_pMCUBufR[o];
          tmp[cnt * 3 + 1] = gray ? info.m_pMCUBufR[o] : info.m_pMCUBufG[o];
          tmp[cnt * 3 + 2] = gray ? info.m_pMCUBufR[o] : info.m_pMCUBufB[o];
          cnt++;
        }
        sink_span(y, mx * bw, cnt, tmp);
      }
    } else {
      for (unsigned y = 0; y < mh; y++) {
        const unsigned ay = my * mh + y;
        if (ay >= h) break;
        unsigned cnt = 0;
        for (unsigned x = 0; x < mw; x++) {
          if (mx * mw + x >= w) break;
          const unsigned o = (x >> 3) * 64 + (y >> 3) * 128 + (y & 7) * 8 + (x & 7);
          tmp[cnt * 3] = info.m_pMCUBufR[o];
          tmp[cnt * 3 + 1] = gray ? info.m_pMCUBufR[o] : info.m_pMCUBufG[o];
          tmp[cnt * 3 + 2] = gray ? info.m_pMCUBufR[o] : info.m_pMCUBufB[o];
          cnt++;
        }
        sink_span(ay, mx * mw, cnt, tmp);
      }
    }
    if (++mx == info.m_MCUSPerRow) {
      mx = 0;
      my++;
      sink_rows_done(reduce ? my * bh : my * mh);
      if (my >= info.m_MCUSPerCol) break;
    }
  }
  sink_rows_done(oh);
  return NULL;
}
