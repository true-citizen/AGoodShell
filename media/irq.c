/*
 * VBlank interrupt of the MP3 player (ARM code, runs from the IWRAM): feeds the Direct Sound
 * channel A. Each frame has exactly 304 samples: the timer period is 924 cycles and a frame
 * lasts 280896 cycles (304 * 924), so restarting the DMA and the timer on every VBlank keeps
 * the playback in sync with the screen without any drift (18157 Hz).
 */
#include <stdint.h>
#include "player.h"

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))

int8_t ring[RING_SIZE] __attribute__((aligned(4), section(".ibss")));
volatile uint32_t ring_wr, ring_rd;               // Free running counters (bytes)
volatile uint8_t out_paused;
volatile uint32_t frame_count, underruns;
static int8_t dmabuf[2][FRAME_SAMPLES] __attribute__((aligned(4), section(".ibss")));
static unsigned cur;

void vblank_handler(void) {
  // Restart the DMA on the buffer that was filled during the previous frame, with the timer
  REG16(0x040000C6) = 0;
  REG32(0x040000BC) = (uint32_t)dmabuf[cur];
  REG32(0x040000C0) = 0x040000A0;                 // FIFO A
  REG16(0x040000C6) = 0xB640;                     // Enabled, special timing, repeat, 32 bit, dest fixed
  REG16(0x04000102) = 0;
  REG16(0x04000100) = 65536 - PERIOD_CYCLES;
  REG16(0x04000102) = 0x80;

  // Fill the other one with the next samples
  cur ^= 1;
  uint32_t *dst = (uint32_t*)dmabuf[cur];
  const uint32_t avail = ring_wr - ring_rd;
  frame_count++;
  if (out_paused || avail < FRAME_SAMPLES) {
    if (!out_paused)
      underruns++;
    for (unsigned i = 0; i < FRAME_SAMPLES / 4; i++)
      dst[i] = 0;
  } else {
    uint32_t rd = ring_rd;
    for (unsigned i = 0; i < FRAME_SAMPLES / 4; i++, rd += 4)
      dst[i] = *(uint32_t*)&ring[rd & (RING_SIZE - 1)];     // The ring is 4 byte aligned
    ring_rd = rd;
  }
}

// The BIOS jumps here (in ARM state, IRQ mode) with r0-r3, r12 and lr saved.
void irq_handler(void) {
  const uint16_t flags = REG16(0x04000200) & REG16(0x04000202);
  REG16(0x04000202) = flags;                      // Acknowledge
  REG16(0x03007FF8) |= flags;                     // For the BIOS wait functions
  if (flags & 1)
    vblank_handler();
}
