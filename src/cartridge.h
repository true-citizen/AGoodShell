/*
 * Cartridge view of the game browser: a picture of a GBA cartridge (res/cartridge.png)
 * with the box art of the game in its label window and a plastic color that can be
 * chosen per game.
 *
 * Box art lives in /ART on the SD card as BMP files (uncompressed, 8, 24 or 32 bits per
 * pixel), named after the ROM file without the extension (Game.bmp for Game.gba) or after
 * the 4 character game code of a GBA ROM (AXVE.bmp). The size is the one of the label window
 * of the cartridge picture of the platform (GBA 150x80, see docs); other sizes are scaled. The plastic
 * color of a game comes from /ART/colors.txt, with lines such as "AXVE=red".
 */

#ifndef _CARTRIDGE_H_
#define _CARTRIDGE_H_

#include <stdint.h>
#include <stdbool.h>

#define CART_W           240      // The cartridge picture covers the whole screen
#define CART_H           160
#define CART_ART_MAX     (150 * 80) // Largest box art (pixels)
#define CART_STRIDE(w)   (((w) + 1) & ~1) // Box art rows are padded to an even width (16-bit writes)
#define CART_RLE_MAX     1024     // Largest unpacked cartridge picture

#define CART_PAL_PLASTIC 48       // 3 colors (plastic, highlight, shadow) for each of 2 carts
#define CART_PAL_PIN     54       // Metal contacts of the cartridges that have them (yellow)
#define CART_PAL_ART     136      // Palette of the box art: 5 reds x 6 greens x 3 blues
#define CART_PAL_ART_CNT 90       // (kept clear of the 240.. entries of the in-game menu)

#define CART_DEFAULT_RGB 0x2d2d2d // Plastic color of the pictures as drawn

// (The SD build, short of flash space, only has the first four pictures: -DCART_FEW_PICS.)
// Cartridge types: each one has its own picture and box art size (the size of its label window).
enum { CART_GBA, CART_GB, CART_GBC, CART_NES, CART_SV, CART_GG, CART_PCE, CART_NGC, CART_SMS };

typedef struct {
  uint8_t x, y, w, h;             // Label window (box art) position in the picture and size
} t_cartwin;

typedef struct {
  int dx;                         // Horizontal offset of the cartridge on screen
  uint8_t pal;                    // First of the 3 plastic palette entries to use
  uint8_t fill;                   // Color of the label window when there is no art
  const uint8_t *rle;             // Unpacked picture (see cart_unpack)
  t_cartwin win;                  // Label window of the picture
  const uint8_t *art;             // Box art (win.w x win.h palette indices) or NULL
} t_cartview;

// Cartridge type for a file extension (without the dot): GBA for anything unknown.
unsigned cart_type(const char *ext);
// Label window of a cartridge type.
t_cartwin cart_window(unsigned type);
// Unpacks the picture of a cartridge type (dst has CART_RLE_MAX bytes and is 16-bit writable).
void cart_unpack(unsigned type, uint8_t *dst);

// Sets the palette entries of the box art.
void cart_init_palette();
// Sets the plastic palette entries pal..pal+2 for a color (0xRRGGBB).
void cart_set_plastic(unsigned pal, uint32_t rgb);
// Draws the whole picture (rows 0..CART_H-1 starting at the given frame row) for one or two
// cartridges, over a background of the given color.
void cart_render(volatile uint8_t *rows, const t_cartview *views, unsigned count, uint8_t bg);

// Loads and converts a BMP into the box art format (dst needs to be 16-bit writable). The
// window size is the one of the cartridge type; a BMP of another size is scaled to it.
bool cart_load_art(const char *path, uint8_t *dst, t_cartwin win);
// Reads the game code of a GBA ROM (5 bytes, nul terminated).
bool cart_read_gcode(const char *path, char *gcode);
// Looks up the plastic color of a game by name (ROM name without extension) first and
// by game code (can be NULL) second in /ART/colors.txt.
bool cart_find_color(const char *name, const char *gcode, uint32_t *rgb);

#endif
