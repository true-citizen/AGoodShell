#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H
#include <stdint.h>

#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))
#define VRAM     ((volatile uint16_t*)0x06000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

// Header the firmware writes after the player ROM (the size of the ROM is in the ROM header)
typedef struct {
  char magic[4];              // "AMP3" (an .mp3 file follows) or "AIMG" (a .png, .jpg or .gif)
  uint32_t size;              // Size of the file
  char title[32];             // File name
  uint16_t bg, ft, grid;      // Theme colors of the firmware (RGB555): background, font, grid lines
  uint16_t dark;              // Dark theme
} t_media_header;

extern uint16_t C_FG, C_DIM, C_ACC, C_WARN;   // Text colors, from the theme (see theme_init)
void theme_init(const t_media_header *h);
void bg_paint(int x0, int y0, int x1, int y1);   // The grid background of the firmware menu
unsigned text_width(const char *s);

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
