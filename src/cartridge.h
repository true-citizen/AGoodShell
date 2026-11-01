/*
 * Cartridge view of the game browser: a picture of a GBA cartridge (res/cartridge.png)
 * with the box art of the game in its label window and a plastic color that can be
 * chosen per game.
 *
 * Box art lives in /ART on the SD card as 150x80 BMP files (uncompressed, 8, 24 or
 * 32 bits per pixel), named after the ROM file without the extension (Game.bmp for
 * Game.gba) or after the 4 character game code of the ROM (AXVE.bmp). The plastic
 * color of a game comes from /ART/colors.txt, with lines such as "AXVE=red".
 */

#ifndef _CARTRIDGE_H_
#define _CARTRIDGE_H_

#include <stdint.h>
#include <stdbool.h>

#define CART_W           240      // The cartridge picture covers the whole screen
#define CART_H           160
#define CART_ART_X       45       // Position of the label window in the picture
#define CART_ART_Y       51
#define CART_ART_W       150      // Box art size (the label window of the picture)
#define CART_ART_H       80

#define CART_PAL_PLASTIC 48       // 3 colors (plastic, highlight, shadow) for each of 2 carts
#define CART_PAL_ART     136      // Palette of the box art: 5 reds x 6 greens x 3 blues
#define CART_PAL_ART_CNT 90       // (kept clear of the 240.. entries of the in-game menu)

#define CART_DEFAULT_RGB 0x2d2d2d // Plastic color of the picture as drawn

typedef struct {
  int dx;                         // Horizontal offset of the cartridge on screen
  uint8_t pal;                    // First of the 3 plastic palette entries to use
  uint8_t fill;                   // Color of the label window when there is no art
  const uint8_t *art;             // Box art (CART_ART_W x CART_ART_H palette indices) or NULL
} t_cartview;

// Sets the palette entries of the box art.
void cart_init_palette();
// Sets the plastic palette entries pal..pal+2 for a color (0xRRGGBB).
void cart_set_plastic(unsigned pal, uint32_t rgb);
// Draws the whole picture (rows 0..CART_H-1 starting at the given frame row) for one or two
// cartridges, over a background of the given color.
void cart_render(volatile uint8_t *rows, const t_cartview *views, unsigned count, uint8_t bg);

// Loads and converts a BMP into the box art format (dst needs to be 16-bit writable).
bool cart_load_art(const char *path, uint8_t *dst);
// Reads the game code of a GBA ROM (5 bytes, nul terminated).
bool cart_read_gcode(const char *path, char *gcode);
// Looks up the plastic color of a game by name (ROM name without extension) first and
// by game code (can be NULL) second in /ART/colors.txt.
bool cart_find_color(const char *name, const char *gcode, uint32_t *rgb);

#endif
