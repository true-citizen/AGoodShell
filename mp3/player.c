/*
 * MP3 player for the GBA. A standalone ROM: the firmware loads it like an emulator, after the
 * ROM come a small header (see mp3_header() in src/emu.c) and the .mp3 file.
 *
 * Decoding is done by libmad (fixed point, MPEG-1/2/2.5 Layer III) in the main loop; the
 * result is resampled to 18157 Hz, converted to 8 bit and played through Direct Sound A (see
 * irq.c). This is as much as a GBA can do: a mono 16 kHz / 16 kbps stream uses about 60% of
 * the CPU, higher sample rates and stereo are too much (see docs/multimedia.md). The decoder
 * keeps measuring how busy the CPU is and shows it, along with the buffer underruns.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mad.h"
#include "player.h"
#include "../src/thumbstart_font.h"

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define VRAM     ((volatile uint16_t*)0x06000000)

// Header the firmware writes after the player ROM (the size of the ROM is in the header)
typedef struct {
  char magic[4];              // "AMP3"
  uint32_t size;              // Size of the .mp3 file
  char title[32];             // File name
} t_mp3_header;

// SuperCard mapping register (see src/supercard_driver.c): the magic value twice, then the mode.
// This runs from the EWRAM, the ROM area is going to change.
static void write_supercard_mode(uint16_t value) {
  __asm__ volatile (
    "strh %1, [%0]\n strh %1, [%0]\n strh %2, [%0]\n strh %2, [%0]\n"
    :: "l"(0x09FFFFFE), "l"(0xA55A), "l"(value) : "memory");
}

// ---------------------------------------------------------------------------
// Display (mode 3, text with the 8x16 font of the Thumbstart loader)
// ---------------------------------------------------------------------------

#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define C_BG    RGB(2, 2, 6)
#define C_FG    RGB(28, 28, 31)
#define C_DIM   RGB(14, 14, 18)
#define C_ACC   RGB(10, 26, 14)
#define C_WARN  RGB(31, 10, 6)

static void rect(int x0, int y0, int x1, int y1, uint16_t c) {
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++)
      VRAM[y * 240 + x] = c;
}

static void text(int x, int y, const char *s, uint16_t fg) {
  for (; *s && x < 232; s++, x += 8) {
    const uint8_t *g = ts_font[(*s >= 0x20 && *s < 0x7F) ? *s - 0x20 : '?' - 0x20];
    for (int r = 0; r < 16; r++)
      for (int b = 0; b < 8; b++)
        VRAM[(y + r) * 240 + x + b] = (g[r] & (0x80 >> b)) ? fg : C_BG;
  }
}

static char *put_uint(char *d, unsigned v, unsigned pad) {
  char tmp[12];
  unsigned n = 0;
  do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
  while (n < pad) tmp[n++] = '0';
  while (n) *d++ = tmp[--n];
  return d;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static struct mad_stream stream;
static struct mad_frame frame;
static struct mad_synth synth;

static const uint8_t *mp3_data;
static unsigned mp3_size, mp3_start;     // File, and where the audio starts (after the ID3v2 tag)
static unsigned mp3_pos;                 // Where the decoder starts reading (for the progress)
static bool done;                        // End of the stream
static unsigned vol = 8;                 // 0..10, 8 is unity

static int32_t ext[1152 + 1];            // Resampler input: the last sample of the previous block + the block
static uint32_t rs_step, rs_pos;
static unsigned src_rate, src_ch, kbps;
static uint32_t out_samples;             // Samples produced since the start (or the last seek)
static uint32_t busy_cycles;             // Cycles spent decoding in the current second

static inline uint32_t now(void) {       // Cycle counter: timers 2 and 3 chained
  uint32_t hi, lo, lo2;
  do { hi = REG16(0x0400010C); lo = REG16(0x04000108); lo2 = REG16(0x04000108); } while (lo2 < lo);
  return (hi << 16) | lo;
}

static int16_t to16(mad_fixed_t s) {
  s >>= (MAD_F_FRACBITS - 15);
  return s > 32767 ? 32767 : s < -32768 ? -32768 : s;
}

// Resamples the block in ext[1..n] to the output rate and queues it, 8 bit signed.
static void emit_block(unsigned n) {
  static const uint16_t volmul[11] = { 0, 8, 16, 32, 48, 64, 96, 128, 192, 256, 384 };
  const uint32_t lim = n << 16, gain = volmul[vol];
  uint32_t wr = ring_wr;
  while (rs_pos < lim) {
    const int32_t a = ext[rs_pos >> 16], b = ext[(rs_pos >> 16) + 1];
    int32_t v = a + (((b - a) * (int32_t)((rs_pos >> 1) & 0x7FFF)) >> 15);
    v = (v * (int32_t)gain) >> 16;                         // 16 bit -> 8 bit with the volume
    ring[wr++ & (RING_SIZE - 1)] = v > 127 ? 127 : v < -128 ? -128 : v;
    rs_pos += rs_step;
  }
  rs_pos -= lim;
  ext[0] = ext[n];
  ring_wr = wr;
  out_samples += 0;
}

// Decodes one frame and queues its samples. Returns false at the end of the stream.
static bool decode_frame(void) {
  while (mad_frame_decode(&frame, &stream)) {
    if (MAD_RECOVERABLE(stream.error))
      continue;                                           // Bad frame or tag: look for the next one
    return false;                                         // Out of data or fatal
  }
  mad_synth_frame(&synth, &frame);

  if (synth.pcm.samplerate != src_rate) {                 // First frame (or a change of rate)
    src_rate = synth.pcm.samplerate;
    rs_step = (uint32_t)(((uint64_t)src_rate << 16) / OUT_RATE);
    kbps = frame.header.bitrate / 1000;
  }
  src_ch = synth.pcm.channels;
  const unsigned n = synth.pcm.length;
  for (unsigned i = 0; i < n; i++) {
    int32_t s = to16(synth.pcm.samples[0][i]);
    if (src_ch == 2)
      s = (s + to16(synth.pcm.samples[1][i])) >> 1;       // Mono mix
    ext[i + 1] = s;
  }
  emit_block(n);
  out_samples += (uint64_t)n * OUT_RATE / src_rate;
  return true;
}

static void start_stream(unsigned pos) {
  mp3_pos = pos;
  mad_stream_buffer(&stream, mp3_data + pos, mp3_size - pos);
  mad_frame_mute(&frame);
  mad_synth_mute(&synth);
  rs_pos = 0;
  ext[0] = 0;
  ring_rd = ring_wr = 0;
}

// Skips an ID3v2 tag at the beginning of the file
static unsigned id3_skip(const uint8_t *d, unsigned size) {
  if (size > 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') {
    unsigned n = (d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9];
    n += 10 + ((d[5] & 0x10) ? 10 : 0);
    return n < size ? n : 0;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------

static void sound_init(void) {
  REG16(0x04000084) = 0x80;                    // Sound on
  REG16(0x04000080) = 0;
  REG16(0x04000082) = 0x0B0F & ~0x0003 | 0x0004 | 0x0300 | 0x0800;  // DS A: full volume, L+R, timer 0, reset FIFO
  REG16(0x04000102) = 0;
  REG16(0x04000100) = 65536 - PERIOD_CYCLES;
  // Cycle counter
  REG16(0x0400010A) = 0; REG16(0x0400010E) = 0;
  REG16(0x04000108) = 0; REG16(0x0400010C) = 0;
  REG16(0x0400010E) = 0x84; REG16(0x0400010A) = 0x80;
}

static void irq_init(void) {
  REG32(0x03007FFC) = (uint32_t)irq_handler;
  REG16(0x04000004) = 0x0008;                  // VBlank IRQ enable
  REG16(0x04000200) = 0x0001;
  REG16(0x04000208) = 1;
}

static void halt(void) { __asm__ volatile("swi 0x02" ::: "r0", "r1", "r2", "r3", "memory"); }

// Back to the firmware (map the flash again and reset through the BIOS).
static void exit_player(void) {
  REG16(0x04000208) = 0;
  REG16(0x04000084) = 0;
  write_supercard_mode(0);                     // Flash mapped, read only, no SD interface
  __asm__ volatile("swi 0x26");
  for (;;) ;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void draw_static(const t_mp3_header *h) {
  REG16(0x04000000) = 0x0403;                  // Mode 3, BG2
  rect(0, 0, 240, 160, C_BG);
  text(8, 8, "MP3 player", C_ACC);
  char name[33];
  memcpy(name, h->title, 32);
  name[32] = 0;
  text(8, 32, name, C_FG);
  text(8, 144, "A:Pause  B:Exit  <>:Seek  ^v:Vol", C_DIM);
}

static void draw_status(bool paused) {
  char buf[40], *p;
  rect(8, 56, 232, 100, C_BG);
  p = buf;
  p = put_uint(p, src_rate / 1000, 0); *p++ = '.'; p = put_uint(p, (src_rate % 1000) / 100, 0);
  *p++ = 'k'; *p++ = 'H'; *p++ = 'z'; *p++ = ' ';
  *p++ = src_ch == 2 ? 'S' : 'M'; *p++ = ' ';
  p = put_uint(p, kbps, 0); *p++ = 'k'; *p++ = 'b'; *p++ = 'p'; *p++ = 's';
  *p = 0;
  text(8, 56, buf, C_DIM);

  const unsigned sec = out_samples / OUT_RATE;
  p = buf;
  p = put_uint(p, sec / 60, 2); *p++ = ':'; p = put_uint(p, sec % 60, 2);
  *p++ = ' '; *p++ = ' ';
  *p++ = 'V'; p = put_uint(p, vol, 0);
  *p = 0;
  text(8, 76, buf, C_FG);
  text(120, 76, paused ? "PAUSED" : done ? "FINISHED" : "PLAYING", paused ? C_WARN : C_ACC);

  // Progress (bytes read)
  const unsigned consumed = (stream.this_frame ? (unsigned)(stream.this_frame - mp3_data) : mp3_pos);
  const unsigned w = (unsigned)(((uint64_t)consumed * 224) / mp3_size);
  rect(8, 104, 232, 112, C_DIM);
  rect(8, 104, 8 + (w > 224 ? 224 : w), 112, C_ACC);
}

static void draw_load(unsigned load, unsigned ur) {
  char buf[40], *p = buf;
  rect(8, 118, 232, 134, C_BG);
  *p++ = 'C'; *p++ = 'P'; *p++ = 'U'; *p++ = ' ';
  p = put_uint(p, load, 0); *p++ = '%'; *p++ = ' ';
  *p++ = 'u'; *p++ = 'n'; *p++ = 'd'; *p++ = 'e'; *p++ = 'r'; *p++ = ':'; *p++ = ' ';
  p = put_uint(p, ur, 0); *p = 0;
  text(8, 118, buf, load > 90 || ur ? C_WARN : C_DIM);
}

int main(void) {
  const uint32_t romsize = REG32(0x080000CC);
  const t_mp3_header *h = (const t_mp3_header*)(0x08000000 + romsize);
  REG16(0x04000000) = 0x0080;                  // Forced blank while we set things up
  if (memcmp(h->magic, "AMP3", 4) || h->size < 128) {
    draw_static(h);
    text(8, 60, "No MP3 data!", C_WARN);
    for (;;) halt();
  }

  mp3_data = (const uint8_t*)&h[1];
  mp3_size = h->size;
  mp3_start = id3_skip(mp3_data, mp3_size);
  draw_static(h);
  sound_init();

  mad_stream_init(&stream);
  mad_frame_init(&frame);
  mad_synth_init(&synth);
  start_stream(mp3_start);

  // Prebuffer before the sound starts
  while (ring_wr < 2048 && decode_frame()) ;
  irq_init();

  bool paused = false;
  unsigned prev_keys = 0xFFFF, last_ui = 0, last_cpu = 0, last_ur = 0;
  uint32_t sec_start = now();
  for (;;) {
    // Keep the ring full
    const uint32_t t0 = now();
    while (!paused && !done && RING_SIZE - (ring_wr - ring_rd) >= 1400 + 4) {
      if (!decode_frame())
        done = true;
    }
    busy_cycles += now() - t0;

    // The end: wait for the queued audio to be played
    if (done && ring_wr - ring_rd < FRAME_SAMPLES)
      out_paused = 1;

    // Keys
    const unsigned keys = ~REG16(0x04000130) & 0x3FF, pressed = keys & ~prev_keys;
    prev_keys = keys;
    if (pressed & 0x02)                                    // B
      exit_player();
    if (pressed & 0x01) {                                  // A
      paused = !paused;
      out_paused = paused;
    }
    if (pressed & 0x40 && vol < 10) vol++;                 // Up
    if (pressed & 0x80 && vol > 0) vol--;                  // Down
    if (pressed & 0x30) {                                  // Right / Left: seek by 5% of the file
      const unsigned step = mp3_size / 20;
      unsigned pos = stream.this_frame ? (unsigned)(stream.this_frame - mp3_data) : mp3_pos;
      pos = (pressed & 0x10) ? pos + step : (pos > step + mp3_start ? pos - step : mp3_start);
      if (pos + 1024 < mp3_size) {
        const bool was = out_paused;
        out_paused = 1;
        start_stream(pos);
        done = false;
        out_paused = was;
      }
    }

    // Screen, a few times a second
    if (frame_count - last_ui >= 15) {
      last_ui = frame_count;
      draw_status(paused);
      if (now() - sec_start >= 16777216) {                 // Every second: CPU load of the decoder
        draw_load(busy_cycles / 167772, underruns - last_ur);
        busy_cycles = 0;
        sec_start = now();
        last_ur = underruns;
      }
    }
    (void)last_cpu;

    if (done && out_paused && !paused)
      paused = true;                                       // Finished: stay here until B
    halt();
  }
}
