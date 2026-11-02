#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H
#include <stdint.h>

#define FRAME_SAMPLES   304                       // Samples per video frame (see irq.c)
#define PERIOD_CYCLES   924                       // Timer period: 16777216 / 924 = 18157 Hz
#define OUT_RATE        18157
#define RING_SIZE       4096                      // Power of two, a multiple of FRAME_SAMPLES isn't needed

extern int8_t ring[RING_SIZE];
extern volatile uint32_t ring_wr, ring_rd;
extern volatile uint8_t out_paused;
extern volatile uint32_t frame_count, underruns;

void irq_handler(void);
#endif
