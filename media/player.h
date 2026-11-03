#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H
#include <stdint.h>

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define VRAM     ((volatile uint16_t*)0x06000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define C_BG    RGB(2, 2, 6)
#define C_FG    RGB(28, 28, 31)
#define C_DIM   RGB(14, 14, 18)
#define C_ACC   RGB(10, 26, 14)
#define C_WARN  RGB(31, 10, 6)

// Header the firmware writes after the player ROM (the size of the ROM is in the ROM header)
typedef struct {
  char magic[4];              // "AMP3" (an .mp3 file follows) or "AIMG" (a .png, .jpg or .gif)
  uint32_t size;              // Size of the file
  char title[32];             // File name
} t_media_header;

void rect(int x0, int y0, int x1, int y1, uint16_t c);
void text(int x, int y, const char *s, uint16_t fg);
char *put_uint(char *d, unsigned v, unsigned pad);
void halt(void);
void exit_player(void);

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
