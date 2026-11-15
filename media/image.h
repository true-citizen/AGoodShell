#ifndef MEDIA_IMAGE_H
#define MEDIA_IMAGE_H
#include <stdint.h>
#include <stdbool.h>
#include "player.h"

#define RGB555(r, g, b) (((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10))
#define MAX_IMG_W   4096
#define MAX_IMG_H   16384

void image_main(const t_media_header *h);       // Shows the picture after the header. Never returns.

// Decoders: they get the whole file (it is in the ROM area) and return NULL or an error message.
const char *png_show(const uint8_t *d, unsigned n);
const char *jpg_show(const uint8_t *d, unsigned n);
const char *gif_show(const uint8_t *d, unsigned n);

// Scratch memory the decoders share (only one of them runs)
#define SCRATCH_SIZE (104 * 1024)
extern uint8_t scratch[SCRATCH_SIZE];

// Output: the picture is drawn into the mode 3 screen. If it is bigger than the screen it is
// scaled down to fit (averaging all pixels that land on the same screen pixel), else it is
// centered 1:1.
void sink_begin(unsigned w, unsigned h, bool nearest);     // nearest: pick pixels instead (any row order)
void sink_span(unsigned y, unsigned x0, unsigned n, const uint8_t *rgb);   // n RGB pixels (3 bytes each)
void sink_rows_done(unsigned y);                            // All the rows before y are complete
void fit_dims(unsigned w, unsigned h, unsigned *dw, unsigned *dh);
void vsync(void);
bool key_b(void);                                           // B pressed: leaves for the firmware
// For decoders that write pixels themselves (nearest mode)
int sink_near_dy(unsigned y);                               // Screen row that takes source row y, or -1
extern unsigned sink_ox, sink_oy, sink_dw;
extern uint16_t sink_dxmap[MAX_IMG_W];                      // nearest: source column of each screen column
#endif
