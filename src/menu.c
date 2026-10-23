/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "compiler.h"
#include "gbahw.h"
#include "patchengine.h"
#include "fatfs/ff.h"
#include "common.h"
#include "settings.h"
#include "util.h"
#include "utf_util.h"
#include "fonts/font_render.h"
#include "nanoprintf.h"
#include "messages.h"
#include "save.h"
#include "cheats.h"
#include "ingame.h"
#include "emu.h"
#include "recent.h"
#include "flash_mgr.h"
#include "flash.h"
#include "sha256.h"
#include "supercard_driver.h"

#include "res/icons.h"
#include "res/logo.h"
#include "res/home_icons.h"

extern t_card_info sd_info;
extern bool fastew;
extern bool slowsd;

enum {
  MENUTAB_HOME,          // DS-style start menu (default tab).
  MENUTAB_RECENT,        // Browses recently loaded ROMs (can be disabled / hidden)
  MENUTAB_ROMBROWSE,     // Browses ROMs and launches games (carousel).
  #ifdef SUPPORT_NORGAMES
  MENUTAB_NORBROWSE,     // Browses Flash games and launches them.
  #endif
  MENUTAB_CLOCK,         // Clock and calendar (DS top screen).
  MENUTAB_SETTINGS,      // General settings / defaults
  MENUTAB_UILANG,        // UI / Language settings
  MENUTAB_TOOLS,         // Tools (advaned menu)
  MENUTAB_INFO,          // Info / About / Updater?
  MENUTAB_MAX,
};

#define ANIM_INITIAL_WAIT     128    // Intial wait (in anim cycles)

#define KEY_REPEAT_INITIAL    384    // Initial wait for key repeat
#define KEY_REPEAT_MID        160    // Next key presses
#define KEY_REPEAT_FAST        96    // End speed
#define KEY_REPEAT_CNT1         5

enum {
  POPUP_NONE,
  POPUP_GBA_LOAD,              // Load a GBA ROM
  POPUP_SAVFILE,               // Load/Store a SAV file
  POPUP_FWFLASH,               // Flash a new firmware image
  POPUP_FILE_MGR,              // Write ROM to flash, delete, hide/unhide...
#ifdef SUPPORT_NORGAMES
  POPUP_GBA_NORWRITE,          // Write a GBA ROM to NOR
  POPUP_GBA_NORLOAD,           // Launch a NOR game
#endif
};

#define BROWSER_ROWS                 8
#define RECENT_ROWS                  10
#define NORGAMES_ROWS                9

// First entries reserved for the logo palette.
#define FG_COLOR         16
#define BG_COLOR         17
#define FT_COLOR         18
#define HI_COLOR         19
#define ACC_COLOR        20      // Accent color (home screen selection)
#define BAR_C0           21      // Info bar gradient (top to bottom), then the bottom line
#define BAR_C1           22
#define BAR_C2           23
#define BAR_C3           24
#define BAR_LN           25
#define BAR_TX           26      // Info bar text (white or black, depending on the accent)
#define BAR_GREEN        27
#define BAR_ORANGE       28
#define SL_BG            29      // Background and grid colors for the scanline rows
#define SL_GRID          30
#define BAR_WHITE        31
#define CAL_TXT          32      // Calendar and analog clock (DS Phat style) colors
#define CAL_RED          33
#define CAL_BLUE         34
#define CAL_HRED         35
#define CAL_HBLUE        36
#define CAL_HGRAY        37
#define CAL_PINK         38
#define CAL_LBLUE        39
#define CAL_LINE         40
#define CLK_HAND         41
#define CLK_PIN          42
#define CLK_TICK         43      // Clock numbers
#define CLK_FGRID        46      // Grid lines over the clock face
#define CAL_BG           47      // Calendar and clock face background (white, dark in the dark theme)
#define GRID_COLOR       44      // Background grid lines (black in the dark theme)
#define BTN_EDGE         45      // Home button outlines
#define GRAD_BASE        64      // Home button gradient: GRAD_N shades, top to bottom
#define GRAD_N           32
#define PL_BASE          120     // Carousel (DSi style) colors: plate gradient, edge, lip,
#define PL_EDGE          126     // blue slot gradient, edge, and the dots of the scroll bar
#define PL_SHADE         127
#define BL_BASE          128
#define BL_EDGE          134
#define GR_DOT           135
#define HOME_ICON_BASE   95      // Palette of the TWiLight Menu++ corner icons (bitmap index i -> 95 + i)

// Sound registers (PSG noise channel), used for the clock tick.
#define SND4_L           (*((volatile uint16_t *) 0x04000078))
#define SND4_H           (*((volatile uint16_t *) 0x0400007C))
#define SNDCNT_L         (*((volatile uint16_t *) 0x04000080))
#define SNDCNT_H         (*((volatile uint16_t *) 0x04000082))
#define SNDCNT_X         (*((volatile uint16_t *) 0x04000084))
#define IGM_PAL_FG      240
#define IGM_PAL_BG      241
#define IGM_PAL_HI      242
#define IGM_PAL_SH      243
#define IGM_PAL_BL      244
#define SEL_COLOR       255

#define FLASH_UNLOCK_KEYS      (KEY_BUTTDOWN|KEY_BUTTB|KEY_BUTTSTA)
#define FLASH_GO_KEYS          (KEY_BUTTUP|KEY_BUTTL|KEY_BUTTR)

enum {
  UiSetTheme = 0,
  UiSetLang  = 1,
  UiSetRect  = 2,
  UiSetASpd  = 3,
  UiSetHid   = 4,
  UiSetAccent = 5,
  UiSetName  = 6,
  UiSetSave  = 7,
  UiSetMAX   = 7,
};

// Accent colors, the 16 favorite colors of the DS.
#define ACCENT_COUNT 16
static const struct { const char *name; uint32_t rgb; } accents[ACCENT_COUNT] = {
  { "Gray",       0x8a8f9a }, { "Brown",    0xa0643c }, { "Red",     0xe03a3a }, { "Pink",    0xf080b0 },
  { "Orange",     0xf08a20 }, { "Yellow",   0xe8c820 }, { "Lime",    0x9ad020 }, { "Green",   0x30b040 },
  { "Dark green", 0x208040 }, { "Teal",     0x20b090 }, { "Cyan",    0x30b8e8 }, { "Blue",    0x3f7fd8 },
  { "Dark blue",  0x2840a8 }, { "Violet",   0x7048d0 }, { "Purple",  0xa040c0 }, { "Magenta", 0xd83090 },
};

// Characters available when editing the user name.
static const char name_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

// Blends two GBA 15 bit colors, t/256 of the way from a to b.
static uint16_t mix15(uint16_t a, uint16_t b, int t) {
  uint16_t r = 0;
  for (unsigned sh = 0; sh < 15; sh += 5) {
    const int ca = (a >> sh) & 31, cb = (b >> sh) & 31;
    r |= (uint16_t)((ca + (((cb - ca) * t) >> 8)) << sh);
  }
  return r;
}

enum {
  ToolsSDRAMTest = 0,
  ToolsSRAMTest,
  ToolsBatteryTest,
  ToolsSDBench,
  ToolsFlashBak,
  #ifdef SUPPORT_NORGAMES
  ToolsFlashClr,
  #endif
  ToolsMAX,
};

// Carousel tab layout (DSi menu style), in pixels.
#define CAR_BUB_Y        24      // Speech bubble with the name of the entry (the tab is
#define CAR_BUB_H        46      // drawn 16 rows up, so these are screen rows + 16)
#define CAR_SLOT_Y       76      // Blue slot of the selected entry
#define CAR_SLOT_W       64
#define CAR_SLOT_H       72
#define CAR_PLATE_Y      82      // Top of the white plates
#define CAR_PITCH        56      // Distance between the centers of two plates
#define CAR_BOX_W        48      // Plate size
#define CAR_BOX_H        48
#define CAR_ICON_SZ      32      // File icon size
#define CAR_ICON_Y       6       // Icon offset from the top of the plate
#define CAR_BAR_Y        160     // Scroll bar along the bottom: arrow buttons, track, thumb
#define CAR_BAR_H        16
#define CAR_TRK_X0       16
#define CAR_TRK_X1       224
#define CAR_THUMB        14
#define CAR_PAGE          4      // Entries skipped by UP/DOWN
#define CAR_SPEED_DIV     4      // Easing: move 1/4 of the remaining distance
// Clock tab:
#define CLK_FACE_X       8       // Analog clock face: 6x6 tiles of 16x16, centered
#define CLK_FACE_Y       40
#define CLK_FACE_SZ      96
#define CLK_CX           (CLK_FACE_X + CLK_FACE_SZ / 2)   // Pin position
#define CLK_CY           (CLK_FACE_Y + CLK_FACE_SZ / 2)
#define CAL_X           120      // Calendar: 7 columns of 16x16 tiles up to the screen edge
#define CAL_TILE         16
#define CAL_Y            24      // Calendar top: title row, weekday row and up to 6 week rows

enum {
  SettTitle1 = 0,
  SettHotkey,
  SettBootType,
  SettFastSD,
  #ifdef SUPPORT_NORGAMES
  SettVerifyNOR,
  #endif
  SettFastEWRAM,
  SettSaveLoc,
  #ifdef SUPPORT_NORGAMES
  SettSaveLocNOR,
  #endif
  SettSaveBkp,
  SettStateLoc,
  SettCheatEn,
  SettTitle2,
  DefsPatchEng,
  DefsGamMenu,
  DefsRTCEnb,
  DefsRTCVal,
  DefsRTCSpeed,
  DefsLoadPol,
  DefsSavePol,
  DefsPrefDS,
  SettSave,
  SettMAX
};

enum {
  DefsSave     = 4,
  DefsMAX      = 4,
};

enum {
  GbaLoadPopInfo  = 0,
  GbaLoadPopLoadS = 1,
  GbaLoadPopPatch = 2,
  GbaLoadCNT      = 3,

  GbaNorWrPatch   = 1,
  GbaNorWrCNT     = 2,

  GbaNorLoad      = 1,
  GbaNorLoadCNT   = 2,
};

enum {
  GBAInfoCNT   = 1,
  GBALoadButt  = 0,

  GBALdSetCNT    = 5,
  GBALdSetLoadP  = 0,
  GBALdSetSaveP  = 1,
  GBALdSetRTC    = 2,
  GBALdSetCheats = 3,
  GBALdRemember  = 4,

  GBAPatchCNT  = 5,
  GBALoadPatch = 0,
  GBASavePatch = 1,
  GBAInGameMen = 2,
  GBARTCPatch  = 3,
  GBAPatchGen  = 4,
};

enum {
  SaveWrite  = 0,
  SavLoad    = 1,
  SavClear   = 2,
  SavQuit    = 3,
  SavMAX     = 3,
};

enum {
  FlashingReady    = 0,
  FlashingLoading  = 1,
  FlashingChecking = 2,
  FlashingErasing  = 3,
  FlashingWriting  = 4,
};

enum {
  FiMgrDelete,
  FiMgrHide,
#ifdef SUPPORT_NORGAMES
  FiMgrWriteNOR,
#endif
  FiMgrCNT
};

const struct {
  uint16_t fg_color;     // Foreground elements color
  uint16_t bg_color;     // Background color
  uint16_t ft_color;     // Font color
  uint16_t hi_color;     // Item/Buttom highlight
  uint16_t hi_blend;     // Menu highlight color (browser)
  uint16_t sh_color;     // Menu shadow/disabled color
} themes[] = {
  { RGB2GBA(0xaaaaaa), RGB2GBA(0xffffff), RGB2GBA(0x000000), RGB2GBA(0xcccccc), RGB2GBA(0x9999bb), RGB2GBA(0xc08888) }, // White
  { RGB2GBA(0xeca551), RGB2GBA(0xe7c092), RGB2GBA(0x000000), RGB2GBA(0xbda27b), RGB2GBA(0x90816e), RGB2GBA(0x615d58) }, // Orange
  { RGB2GBA(0x26879c), RGB2GBA(0x8fb1b8), RGB2GBA(0x000000), RGB2GBA(0x5296a5), RGB2GBA(0x1d7f95), RGB2GBA(0x6f8185) }, // Blue
  { RGB2GBA(0x308855), RGB2GBA(0x88aa99), RGB2GBA(0x000000), RGB2GBA(0x778888), RGB2GBA(0x777777), RGB2GBA(0x606060) }, // Green
  { RGB2GBA(0xad11c8), RGB2GBA(0xe47af6), RGB2GBA(0x000000), RGB2GBA(0xad5dc6), RGB2GBA(0x724095), RGB2GBA(0x72667a) }, // Purple
  { RGB2GBA(0x222222), RGB2GBA(0x444444), RGB2GBA(0xeeeeee), RGB2GBA(0x737573), RGB2GBA(0xaaaaaa), RGB2GBA(0x606060) }, // Dark
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))
#define THEME_LIGHT  0                   // Indexes in themes[], used by the home screen
#define THEME_DARK   (THEME_COUNT - 1)

typedef struct {
  // ROM information
  char romfn[MAX_FN_LEN];             // File to load/write
  uint32_t romfs;                     // File ROM size
  char gcode[5];                      // ASCII sanitized game code.
  t_rom_header romh;                  // ROM header (for info purposes)
  // Patching info
  t_patch patches_datab;              // Loaded patches (from DB)
  t_patch patches_cache;              // Loaded patches (from patch engine's cache)
  bool patches_datab_found;           // Whether we had a patch match in the database
  bool patches_cache_found;           // Same but for the patch cache
  // Patching configuration
  t_patch_policy patch_type;          // Patching type
  bool use_dsaving;                   // Whether we use direct-saving mode
  bool ingame_menu_enabled;           // Enable the in-game menu.
  bool rtc_patch_enabled;             // Patch for RTC workarounds.
} t_load_gba_info;

typedef struct {
  // Save read/write policies and info
  t_sram_load_policy sram_load_type;  // SRAM loading policy
  t_sram_save_policy sram_save_type;  // SRAM auto-saving policy
  char savefn[MAX_FN_LEN];            // Save file path.
  bool savefile_found;                // Whether there's a .sav file.
  // RTC config
  uint32_t rtcval;                    // Initial RTC value.
  // Cheats policy
  bool use_cheats;                    // Whether we want to load cheats to use them.
  bool cheats_found;                  // Whether there's a cheats file (not parsed tho!)
  unsigned cheats_size;               // Size of the cheat buffer
  char cheatsfn[MAX_FN_LEN];          // Cheats file path.
} t_load_gba_lcfg;

typedef void (*t_mrender_fn)(volatile uint8_t *frame);
typedef void (*t_mkeyupd_fn)(unsigned newkeys);

// Info and state for the menu tab
static struct {
  uint8_t menu_tab;
  uint8_t prev_tab;               // Tab to go back to when leaving the clock

  unsigned anim_state;            // Animation (text rotation) status.

  // Recent ROMs state
  struct {
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    int maxentries;               // Total file/dir count in current dir
  } recent;

  // ROM browser state
  struct {
    char cpath[MAX_FN_LEN];       // Current path
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    int maxentries;               // Total file/dir count in current dir
    int dispentries;              // Maximum number of visible entries (filtered)
    uint16_t selhist[16];         // History of directory offsets
  } browser;

  // Flash ROM browser state
  struct {
    int selector;                 // Pointed file offset
    int seloff;                   // Entry at the top of the list
    uint8_t maxentries;           // Total file/dir count in current dir
    uint8_t usedblks, freeblks;   // NOR usage info
  } fbrowser;

  // UI settings
  struct {
    int selector;                 // Pointed option
    uint8_t editing;              // Editing the user name
    uint8_t cursor;               // Character being edited
    char oldname[USERNAME_MAX + 1];
  } uiset;

  // Main settings
  struct {
    int selector;                 // Pointed option
  } set;

  // Tools menu
  struct {
    int selector;                 // Render panel
  } tools;

  // Info/About menu
  struct {
    int selector;                 // Render panel
    char tstr[64];                // Temp message render
  } info;

  // Home screen
  struct {
    int sel;                      // Selected button (HB_*)
    int row2;                     // Middle row button to return to (HB_RECENT/HB_HWTEST)
  } home;
} smenu;

// Same but for popups.
static struct {
  const char *alert_msg;          // Extra pop-up message

  uint8_t pop_num;                // Current pop-up in display
  char submenu;                   // Which submenu tab we are in (if any)
  char selector;                  // Option selector (if any)
  unsigned anim;                  // Animation state

  // Pop up message (for whatever action). Allows returning to previous popup.
  struct {
    const char *message;
    const char *default_button;
    const char *confirm_button;
    void (*callback)(bool confirm);       // Function to call on "confirm".
    uint8_t option;                       // Selected button
    bool clear_popup_ok;                  // Whether any pop up must be cleared.
  } qpop;

  // RTC time set pop up, a bit special.
  struct {
    t_dec_date val;
    int selector;
    void (*callback)();                   // Function to call on "save"
    const char *title;                    // Optional title (default if NULL)
  } rtcpop;

  union {
    // GBA launch ROM pop up menu
    struct {
      t_load_gba_info i;                  // ROM/Patch info and patch policy.
      t_load_gba_lcfg l;                  // ROM loading info and settings;
    } load;

    // Write GBA game to NOR memory
    struct {
      t_load_gba_info i;                  // ROM/Patch info and patch policy.
    } norwr;
    // Launch GBA game from NOR memory
    struct {
      t_load_gba_lcfg l;                  // ROM loading info and settings;
      const t_flash_game_entry *e;        // NOR game entry on RAM
    } norld;

    // Save file menu (.sav files)
    struct {
      char savfn[MAX_FN_LEN];             // SAV file to load/store/mangle
    } savopt;
    // Update menu (for .fw files)
    struct {
      char fn[MAX_FN_LEN];                // FW file to load and flash
      bool issfw;                         // The firmware is a superFW image.
      uint32_t superfw_ver;               // Reported FW version.
      uint32_t fw_size;                   // Size in bytes reported by stat.
      unsigned curr_state;                // Flashing FSM state.
    } update;

    // Not really a pop up, but used as "popup" data for menu questions.
    struct {
      char fn[MAX_FN_LEN];
      unsigned fs;
    } pdb_ld;
  } p;
} spop;

typedef struct {
  uint32_t filesize;
  uint16_t isdir;
  uint16_t attr;
  char fname[MAX_FN_LEN];
  uint16_t sortname[MAX_FN_LEN];       // Pre-decoded and sort-friendly name.
} t_centry;
_Static_assert (sizeof(t_centry) % 4 == 0, "t_centry must be word-friendly");

// Pointer to SDRAM, where we place some data:
//  - Scratch area 2MiB (for FW updates)
//  - File list order (~64KiB)
//  - Browser file information (~13MB)
//  - Recently played ROMs table (~64KiB)
//  - Font data (placed by the bootloader at the 15..16MB range)
// At the end of the SDRAM, ro-data can be loaded by the loader.
#define scratch_mem_size (2*1024*1024)
typedef struct {
  uint8_t scratch[scratch_mem_size];
  t_centry *fileorder[BROWSER_MAXFN_CNT];
  t_centry fentries[BROWSER_MAXFN_CNT];
  t_rentry rentries[RECENT_MAXFN_CNT];
  t_reg_entry_max nordata;
} t_sdram_state;

_Static_assert (sizeof(t_sdram_state) <= 14.5*1024*1024, "scratch SDRAM doesn't exceed 14.5MB");

t_sdram_state *sdr_state = (t_sdram_state*)0x08000000;
uint8_t *hiscratch = (uint8_t*)ROM_HISCRATCH_U8;

typedef struct {
  uint16_t x, y;
  unsigned tn;
} t_oamobj;

static bool enable_flashing = false;
static unsigned framen = 0;
static unsigned objnum = 0;
static t_oamobj fobjs[64];

unsigned lang_lookup(uint16_t code) {
  for (unsigned i = 0; i < LANG_COUNT; i++)
    if (lang_codes[i] == code)
      return i;

  return 0;  // Fallback to default (english)
}

inline uint16_t curr_pressed_keys() {
  return REG_KEYINPUT ^ 0x3FF;
}

uint16_t lang_getcode() {
  return lang_codes[lang_id];
}

inline bool isascii(char code) {
  // Abuse signed :D
  return code >= 32;
}

bool is_superfw(const t_rom_header *h) {
  return !memcmp(&h->data[SUPERFW_COMMENT_DOFFSET], "SUPERFW~DAVIDGF", 16);
}

static int strcmp16(const uint16_t *a, const uint16_t *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

NOINLINE int filesort(const void *a, const void *b) {
  const t_centry *ca = *(t_centry**)a;
  const t_centry *cb = *(t_centry**)b;

  // Directories some up first.
  if (ca->isdir != cb->isdir)
    return cb->isdir - ca->isdir;

  // Other files are string-ordered
  return strcmp16(ca->sortname, cb->sortname);
}

NOINLINE int romsort(const void *a, const void *b) {
  const t_flash_game_entry *ca = (t_flash_game_entry*)a;
  const t_flash_game_entry *cb = (t_flash_game_entry*)b;

  return strcasecmp(&ca->game_name[ca->bnoffset], &cb->game_name[cb->bnoffset]);
}

static void loadrom_progress(unsigned done, unsigned total) {
  // Draws and flips the buffer, do not care about vsync here
  volatile uint8_t *frame = &MEM_VRAM_U8[0xA000*framen];

  // Render the full background to a solid color
  dma_memset16(&frame[0], dup8(BG_COLOR), SCREEN_WIDTH*SCREEN_HEIGHT/2);

  // Render a simple progress bar
  unsigned prog = done * 200 / total;
  for (unsigned i = 76; i < 84; i++)
    dma_memset16(&frame[SCREEN_WIDTH * i + 20], dup8(FG_COLOR), prog/2);

  dma_memset16(MEM_OAM, 0, 256);  // Clear icons

  REG_DISPCNT = (REG_DISPCNT & ~0x10) | (framen << 4);
  framen ^= 1;
}

static bool loadrom_progress_abort(unsigned done, unsigned total) {
  loadrom_progress(done, total);

  // Capture A/B buttons to abort the progress
  return ((~REG_KEYINPUT) & KEY_BUTTSTA);
}


bool generate_patches_progress(const char *fn, unsigned fs) {
  // Open ROM and load it in the SDRAM. We load it in 4MB chunks. Not ideal but
  // we want to preserve the data loaded in the SDRAM (ie. fonts).
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return false;

  t_patch_builder pb;
  patchengine_init(&pb, fs);
  const unsigned max_hiscratch = 8*1024*1024;

  for (unsigned i = 0; i < fs; i += max_hiscratch) {
    for (unsigned j = 0; j < max_hiscratch && i + j < fs; j += 4096) {
      UINT rdbytes;
      uint32_t tmp[4096/4];
      if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes))
        return false;

      set_supercard_mode(MAPPED_SDRAM, true, false);
      dma_memcpy32(&hiscratch[j], tmp, sizeof(tmp)/4);
      set_supercard_mode(MAPPED_SDRAM, true, true);
      if (j & ~0xFFFF)
        loadrom_progress((i*2 + j) >> 8, fs >> 7);
    }
    // Amount to process.
    unsigned blksize = MIN(max_hiscratch, fs - i);

    void upd_pe_prog(unsigned prog) {
      unsigned p = i*2 + blksize + prog*4;
      loadrom_progress(p >> 8, fs >> 7);
    }

    // Process patches. Adds them to the existing patchset.
    set_supercard_mode(MAPPED_SDRAM, true, false);
    patchengine_process_rom((uint32_t*)hiscratch, blksize, &pb, upd_pe_prog);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }

  f_close(&fd);
  patchengine_finalize(&pb);

  WRITE_LOG("Patch engine done. Found wcnt: %d save: %d (save mode: %d) irqh: %d rtc: %d",
            pb.p.wcnt_ops, pb.p.save_ops, pb.p.save_mode, pb.p.irqh_ops, pb.p.rtc_ops);

  // Proceed to write patches to their cache.
  return write_patches_cache(fn, &pb.p);
}

bool dump_flashmem_backup() {
  f_mkdir(SUPERFW_DIR);

  // Use a different file name to ensure we do not overwrite firmwares by
  // accident. This adds some minimal overhead.
  SHA256_State st;
  sha256_init(&st);

  FIL fd;
  FRESULT res = f_open(&fd, FLASHBACKUPTMP_FILEPATH, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK)
    return false;

  const unsigned fsize = flashinfo.size ? flashinfo.size : FW_MAX_SIZE_KB*1024;
  for (unsigned i = 0; i < fsize; i += 4*1024) {
    const uint8_t *faddr = (uint8_t*)(ROM_FLASHFIRMW_ADDR + i);

    uint32_t tmp[4096/4];
    set_supercard_mode(MAPPED_FIRMWARE, true, false);
    dma_memcpy32(tmp, faddr, 1024);
    set_supercard_mode(MAPPED_SDRAM, true, true);

    sha256_transform(&st, tmp, sizeof(tmp));

    UINT wrbytes;
    if (FR_OK != f_write(&fd, tmp, sizeof(tmp), &wrbytes) || wrbytes != sizeof(tmp)) {
      f_close(&fd);
      return false;
    }

    loadrom_progress(i >> 10, fsize >> 10);
  }

  f_close(&fd);

  // Calculate the final hash, use a hash prefix as the filename.
  uint8_t h256[32];
  sha256_finalize(&st, h256);

  char finalfn[64];
  npf_snprintf(finalfn, sizeof(finalfn), FLASHBACKUP_FILEPTRN,
               h256[0], h256[1], h256[2], h256[3]);
  f_rename(FLASHBACKUPTMP_FILEPATH, finalfn);

  return true;
}

void patch_gen_callback(bool confirm);

void sram_battery_test_callback(bool confirm) {
  if (confirm) {
    // Fill SRAM with some pseudorandom data to test later.
    sram_pseudo_fill();
    // Program a check on the next reboot!
    program_sram_check();

    spop.alert_msg = msgs[lang_id][MSG_SRAMTST_RDY];
  }
}


static const t_patch * get_game_patch(const t_load_gba_info *info) {
  return info->patch_type == PatchDatabase && info->patches_datab_found ? &info->patches_datab :
         info->patch_type == PatchEngine   && info->patches_cache_found ? &info->patches_cache : NULL;
}

bool ingame_menu_avail_sdram(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);
  // Necessary size to load the IGM (+fonts +cheats)
  const unsigned igm_reqsz = ROUND_UP2(ingame_menu_payload.menu_rsize + font_block_size() + spop.p.load.l.cheats_size, 1024);

  // If the ROM is too big, must use some hole to load the menu.
  if (info->romfs > MAX_GBA_ROM_SIZE - igm_reqsz) {
    // Discard holes that are too small, or not well formed.
    if (!p || p->hole_size < igm_reqsz || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit the menu!
  }

  // Check if the patches exist and have proper IRQ support.
  return p && p->irqh_ops > 0;
}

bool ingame_menu_avail_flash(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Checks if the ROM is small enough so the last 4MiB block can be remapped.
  if (info->romfs > MAX_GBA_ROM_SIZE - NOR_BLOCK_SIZE) {
    // Otherwise find a gap to flash on NOR our tiny payload
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  // Check if the patches exist and have proper IRQ support.
  return p && p->irqh_ops > 0;
}

// Calculates whether DirectSaving can be used given some information.
bool dirsav_avail_sdram(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Check if there's enough space for it! (Placing it at the end).
  if (info->romfs > MAX_GBA_ROM_SIZE - DIRSAVE_REQ_SPACE) {
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  return (p && supports_directsave(p->save_mode));
}

bool dirsav_avail_flash(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);

  // Checks if the ROM is small enough so the last 4MiB block can be remapped.
  if (info->romfs > MAX_GBA_ROM_SIZE - NOR_BLOCK_SIZE) {
    // Otherwise find a gap to flash on NOR our tiny payload
    if (!p || p->hole_size < DIRSAVE_REQ_SPACE || p->hole_addr + p->hole_size > info->romfs)
      return false;   // Too big to fit!
  }

  return (p && supports_directsave(p->save_mode));
}

bool rtcemu_avail(const t_load_gba_info *info) {
  const t_patch *p = get_game_patch(info);
  return (p && p->rtc_ops);
}

static bool prepare_gba_info(
  t_load_gba_info *info, const t_rom_load_settings *st,
  const char *fn, uint32_t fs,
  bool load_sdram
) {
  // Pre-load ROM header
  if (preload_gba_rom(fn, fs, &info->romh))
    return false;

  // Fill/copy ROM info.
  if (fn != info->romfn)
    strcpy(info->romfn, fn);
  info->romfs = fs;

  // Sanitize the game code for display
  for (unsigned i = 0; i < 4; i++)
    info->gcode[i] = isascii(info->romh.gcode[i]) ? info->romh.gcode[i] : 0x1A;
  info->gcode[4] = 0;

  // Look up patches, have them handy.
  uint8_t gamecode[5] = {
    info->romh.gcode[0], info->romh.gcode[1],
    info->romh.gcode[2], info->romh.gcode[3],
    info->romh.version
  };
  set_supercard_mode(MAPPED_SDRAM, true, false);
  info->patches_datab_found = patchmem_lookup(gamecode, (uint8_t*)ROM_PATCHDB_U8, &info->patches_datab);
  set_supercard_mode(MAPPED_SDRAM, true, true);

  if (!info->patches_datab_found)
    WRITE_LOG("No patches in PatchDB found for '%s' with gamecode %c%c%c%c-%d",
              fn, gamecode[0], gamecode[1], gamecode[2], gamecode[3], (int)gamecode[4]);

  // Attempt to load any existing patch and check also the PE cache dir.
  info->patches_cache_found = load_rom_patches(fn, &info->patches_cache);
  if (!info->patches_cache_found) {
    WRITE_LOG("No patch file found for '%s'", fn);
    info->patches_cache_found = load_cached_patches(fn, &info->patches_cache);
    if (!info->patches_cache_found)
      WRITE_LOG("No patch file found in patches cache dir for '%s'", fn);
  }

  // If PatchAuto is selected, resolve it. Downgrade if not found.
  if (st->patch_policy == PatchAuto) {
    if (info->patches_cache_found)
      info->patch_type = PatchEngine;      // Try existing patches
    else if (info->patches_datab_found)
      info->patch_type = PatchDatabase;    // Try the database then
    else
      info->patch_type = PatchNone;
  }
  else {
    // Use the specified patch type.
    info->patch_type = st->patch_policy;

    // Downgrade to no patches if the specified was not found.
    if ((info->patch_type == PatchDatabase && !info->patches_datab_found) ||
        (info->patch_type == PatchEngine   && !info->patches_cache_found))
      info->patch_type = PatchNone;
  }

  // Fill defaults as requested if possible.
  bool allowds = load_sdram ? dirsav_avail_sdram(info) : dirsav_avail_flash(info);
  bool allowigm = load_sdram ? ingame_menu_avail_sdram(info) : ingame_menu_avail_flash(info);

  info->rtc_patch_enabled = st->use_rtc && rtcemu_avail(info);
  info->use_dsaving = st->use_dsaving && allowds;
  info->ingame_menu_enabled = st->use_igm && allowigm;

  return true;
}

static void prepare_gba_cheats(const char *gcode, uint8_t ver, t_load_gba_lcfg *data, const char *fn, bool prefer_cheats) {
  // Attempt to find a cheat file if cheats are enabled.
  data->cheats_size = 0;
  data->cheats_found = false;
  if (enable_cheats) {
    strcpy(data->cheatsfn, fn);
    replace_extension(data->cheatsfn, ".cht");
    data->cheats_found = check_file_exists(data->cheatsfn);
    if (!data->cheats_found) {
      WRITE_LOG("No cheat file found at '%s'", data->cheatsfn);
      // Create a path using the game ID and version.
      npf_snprintf(data->cheatsfn, sizeof(data->cheatsfn), CHEATS_PATH "%c%c%c%c-%02x.cht",
                   gcode[0], gcode[1], gcode[2], gcode[3], ver);
      data->cheats_found = check_file_exists(data->cheatsfn);
      WRITE_LOG("No cheat file found at '%s'", data->cheatsfn);

      // Load the cheats into memory if enabled.
      if (data->cheats_found) {
        // Load the cheats to the ROM area, just after the font pack. This is for easier relocation.
        uint8_t *cheat_area = (uint8_t*)(ROM_FONTBASE_U8 + font_block_size());
        unsigned max_area = 1536*1024 - font_block_size();    // 1.5MB is reserved at the end.
        int cheatsz = open_read_cheats(cheat_area, max_area, data->cheatsfn);
        if (cheatsz < 0)
          data->cheats_found = false;
        else
          data->cheats_size = cheatsz;

        WRITE_LOG("Loaded cheats returned %d", cheatsz);
      }
    }
  }
  data->use_cheats = enable_cheats && data->cheats_found && prefer_cheats;
}

static void prepare_gba_settings(t_load_gba_lcfg *data, bool uses_dsaving, uint32_t rtcts, bool game_no_save) {
  // Calculate the .sav file name, and check its existance.
  data->savefile_found = check_file_exists(data->savefn);
  if (data->savefile_found)
    WRITE_LOG("Savefile found at '%s'", data->savefn);
  else
    WRITE_LOG("No savefile found for '%s'", data->savefn);

  // Use default settings (and file existance) to fill in default choice.
  // DirectSaving enabled overrides the other settings.
  if (uses_dsaving) {
    data->sram_load_type = data->savefile_found ? SaveLoadSav : SaveLoadReset;
    data->sram_save_type = SaveDirect;
  }
  else {
    data->sram_load_type = game_no_save         ? SaveLoadDisable :
                           !autoload_default    ? SaveLoadDisable :
                           data->savefile_found ? SaveLoadSav :
                                                  SaveLoadReset;
    data->sram_save_type = autosave_default && !game_no_save ? SaveReboot : SaveDisable;
  }

  data->rtcval = rtcts;
}


static void browser_open_gba(const char *fn, uint32_t fs, bool prompt_patchgen) {
  if (fs > MAX_GBA_ROM_SIZE) {
    // The ROM is too big to be loaded!
    spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
  } else {
    // Default to global settings (in case the file is not found).
    t_rom_load_settings ld_sett = {
      .patch_policy = patcher_default,
      .use_igm = ingamemenu_default,
      .use_rtc = rtcpatch_default,
      .use_dsaving = autosave_prefer_ds
    };
    t_rom_launch_settings lh_sett = {
      .use_cheats = true,              // Defaults to true (just preferred, might be disabled/N/A)
      .rtcts = rtcvalue_default
    };
    // Check for any game-specific config file, so we don't have to guess the config.
    // The config file can be partial, hence the defaults.
    load_rom_settings(fn, &ld_sett, &lh_sett);

    if (!prepare_gba_info(&spop.p.load.i, &ld_sett, fn, fs, true))
      spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
    else {
      const t_rom_header *rmh = &spop.p.load.i.romh;

      // If patch engine is selected but no patches found, prompt for generation.
      // If auto is selected and no patches nor DB entries found, do prompt too.
      bool no_patches = (ld_sett.patch_policy == PatchAuto &&
                         !spop.p.load.i.patches_datab_found && !spop.p.load.i.patches_cache_found);
      bool no_engine  = (ld_sett.patch_policy == PatchEngine && !spop.p.load.i.patches_cache_found);
      bool issfw = is_superfw(rmh);

      if (prompt_patchgen && !issfw && (no_patches || no_engine)) {
        // No patches found, ask the user if they want to generate patches
        // using the patch engine.
        spop.qpop.message = msgs[lang_id][no_patches ? MSG_Q1_NOPATCH : MSG_Q1_PATCHENG];
        spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
        spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
        spop.qpop.option = 0;
        spop.qpop.callback = patch_gen_callback;
        spop.qpop.clear_popup_ok = true;
        return;
      }

      // What if the game doesn't have a save method? Select sane defaults.
      const t_patch *p = get_game_patch(&spop.p.load.i);
      bool game_no_save = (p && p->save_mode == SaveTypeNone) || issfw;

      // Attempt to find a cheat file if cheats are enabled.
      prepare_gba_cheats((char*)&rmh->gcode[0], rmh->version, &spop.p.load.l, fn, lh_sett.use_cheats);

      // Load and set default and sane settings honoring defaults and preferences.
      sram_filename_calc(fn, spop.p.load.l.savefn, save_path_default);
      prepare_gba_settings(&spop.p.load.l, spop.p.load.i.use_dsaving, lh_sett.rtcts, game_no_save);

      // Show load ROM menu.
      spop.pop_num = POPUP_GBA_LOAD;
      spop.anim = 0;
      spop.submenu = GbaLoadPopInfo;
      spop.selector = GBALoadButt;
    }
  }
}

#ifdef SUPPORT_NORGAMES
static void browser_open_nor(const t_flash_game_entry * e) {
  // Use attributes to determine patched save method.
  const bool game_no_save = GET_GATTR_SAVEM(e->gattrs) <= SaveTypeNone;
  const bool game_uses_dsaving = (e->gattrs & GATTR_SAVEDS);

  t_rom_launch_settings lh_sett = {
    .use_cheats = true,              // Defaults to true (just preferred, might be disabled/N/A)
    .rtcts = rtcvalue_default
  };
  load_rom_settings(e->game_name, NULL, &lh_sett);

  // Attempt to find a cheat file if cheats are enabled.
  prepare_gba_cheats((char*)&e->gamecode, e->gamever, &spop.p.norld.l, e->game_name, lh_sett.use_cheats);

  // Load and set default and sane settings honoring defaults and preferences.
  sram_filename_calc(e->game_name, spop.p.norld.l.savefn, save_path_nor_default);
  WRITE_LOG("loadp: %d savep: %d uses_ds: %d gamens: %d",
            spop.p.norld.l.sram_load_type, spop.p.norld.l.sram_save_type,
            game_uses_dsaving, game_no_save);
  prepare_gba_settings(&spop.p.norld.l, game_uses_dsaving, lh_sett.rtcts, game_no_save);

  // Save entry pointer
  spop.p.norld.e = e;

  // Show load ROM menu.
  spop.pop_num = POPUP_GBA_NORLOAD;
  spop.submenu = GbaLoadPopInfo;
  spop.selector = 0;
}
#endif

void patch_gen_callback(bool confirm) {
  // Generate patches if confirm was selected
  if (confirm) {
    generate_patches_progress(spop.p.load.i.romfn, spop.p.load.i.romfs);
    spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
  }

  // Either way, show the popup screen afterwards without prompt
  browser_open_gba(spop.p.load.i.romfn, spop.p.load.i.romfs, false);
}

static void load_patchdb_action(bool confirm) {
  if (confirm) {
    FIL fd;
    FRESULT res = f_open(&fd, spop.p.pdb_ld.fn, FA_READ);
    if (res != FR_OK) {
      spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      return;
    } else {
      for (unsigned off = 0; off < spop.p.pdb_ld.fs; off += 1024) {
        UINT rdbytes;
        uint32_t tmp[1024/4];
        if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes)) {
          spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
          return;
        }

        set_supercard_mode(MAPPED_SDRAM, true, false);
        dma_memcpy32(ROM_PATCHDB_U8 + off, tmp, sizeof(tmp)/4);
        set_supercard_mode(MAPPED_SDRAM, true, true);
      }
    }
    spop.alert_msg = msgs[lang_id][MSG_OK_GENERIC];
  }
}

unsigned guess_file_type(const uint8_t *header) {
  const t_rom_header *gbah = (t_rom_header*)header;
  uint32_t sig = *(uint32_t*)header;

  if (gbah->fixed == 0x96 && gbah->unit_code == 0x00 && gbah->devtype == 0x00 &&
      header[3] == 0xEA /* Starts with an unconditional branch */ &&
      validate_gba_header(header))
    return FileTypeGBA;
  else if (validate_gb_header(&header[0x100]))
    return FileTypeGB;
  else if (sig == 0x1A53454E)
    return FileTypeNES;
  else if (sig == 0x31424450)
    return FileTypePatchDB;

  return FileTypeUnknown;
}

static bool insert_recent_flush(const char *fn, unsigned flags) {
  // Insert element.
  smenu.recent.maxentries = insert_recent_fn(sdr_state->rentries, smenu.recent.maxentries, fn, flags);
  return recent_flush(sdr_state->rentries, smenu.recent.maxentries);
}

static bool delete_recent_flush(unsigned entry_num) {
  smenu.recent.maxentries = delete_recent(sdr_state->rentries, smenu.recent.maxentries, entry_num);

  smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector);
  if (!smenu.recent.maxentries)
    smenu.menu_tab = MENUTAB_ROMBROWSE;

  return recent_flush(sdr_state->rentries, smenu.recent.maxentries);
}

static void recent_reload() {
  smenu.recent.selector = 0;
  smenu.recent.seloff = 0;
  smenu.anim_state = 0;
  smenu.recent.maxentries = recent_load(RECENT_FILEPATH, sdr_state->rentries);
}

void start_emu_game(const t_emu_loader *ldinfo, const char *fn, uint32_t fs) {
  // Load: Sav/Reset Save: Reboot/Disable
  sram_filename_calc(fn, spop.p.load.l.savefn, save_path_default);
  t_sram_load_policy lp = check_file_exists(spop.p.load.l.savefn) ? SaveLoadSav : SaveLoadReset;
  unsigned errsave = prepare_sram_based_savegame(lp, SaveReboot, spop.p.load.l.savefn);
  if (errsave) {
    unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                                                        MSG_ERR_SAVEWR;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
  else {
    // Try to load the emu and ROM, keep trying if there's more than one emulatior option.
    unsigned errcode = ERR_LOAD_NOEMU;
    while (ldinfo->emu_name) {
      if (recent_menu)
        insert_recent_flush(fn, FLAG_RECENT_SD);

      errcode = load_extemu_rom(fn, fs, ldinfo, loadrom_progress);
      if (errcode && errcode != ERR_LOAD_NOEMU)
        break;
      ldinfo++;
    }
    unsigned errmsg = (errcode == ERR_LOAD_NOEMU) ? MSG_ERR_NOEMU :
                                                    MSG_ERR_READ;
    spop.alert_msg = msgs[lang_id][errmsg];
  }
}

NOINLINE static void browser_open(const char *fn, uint32_t fs) {
  const char *ext = find_extension(fn);
  if (ext && !strcasecmp(ext, "gba"))
    // GBA ROMs (most likely)
    browser_open_gba(fn, fs, true);
  else if (ext && !strcasecmp(ext, "sav")) {
    spop.pop_num = POPUP_SAVFILE;
    spop.selector = SavMAX;
    strcpy(spop.p.savopt.savfn, fn);
  }
  else if (ext && !strcasecmp(ext, "fw")) {
    // A SuperFW firmware update is selected!
    if (!enable_flashing)
      spop.alert_msg = msgs[lang_id][MSG_FWUP_DISABLED];
    else if (fs > FW_MAX_SIZE_KB*1024 || (flashinfo.size && fs > flashinfo.size))
      spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRSZ];
    else {
      // Read the header and perform some more basic checks!
      FIL fd;
      FRESULT res = f_open(&fd, fn, FA_READ);
      if (res != FR_OK)
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
      else {
        UINT rdbytes;
        uint8_t tmp[512];
        if (FR_OK != f_read(&fd, tmp, sizeof(tmp), &rdbytes) || rdbytes != sizeof(tmp))
          spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
        else if (!validate_gba_header(tmp))  // Is it a valid GBA ROM header?
          spop.alert_msg = msgs[lang_id][MSG_FWUP_BADHD];
        else {
          spop.p.update.issfw = check_superfw(tmp, &spop.p.update.superfw_ver);
          spop.p.update.fw_size = fs;
          spop.p.update.curr_state = FlashingReady;
          spop.pop_num = POPUP_FWFLASH;
          strcpy(spop.p.update.fn, fn);
          f_close(&fd);
        }
      }
    }
  }
  else {
    // Any emulator-based console supported
    if (ext) {
      const t_emu_loader *ldinfo = get_emu_info(ext);
      if (ldinfo) {
        start_emu_game(ldinfo, fn, fs);
        return;
      }
    }

    // Attempt to load the file magic and detect what kind of file this is.
    if (fs >= 512) {
      FIL fi;
      if (FR_OK == f_open(&fi, fn, FA_READ)) {
        uint32_t tmphdr[512 / 4];
        UINT rdbytes;
        if (FR_OK == f_read(&fi, tmphdr, sizeof(tmphdr), &rdbytes) && rdbytes == sizeof(tmphdr)) {
          unsigned guesstype = guess_file_type((uint8_t*)tmphdr);
          switch (guesstype) {
          case FileTypeGBA:
            browser_open_gba(fn, fs, true); break;
          case FileTypeGB:
            start_emu_game(get_emu_info("gbc"), fn, fs);
            break;
          case FileTypePatchDB:
            strcpy(spop.p.pdb_ld.fn, fn);
            spop.p.pdb_ld.fs = fs;
            spop.qpop.message = msgs[lang_id][MSG_Q3_LOADPDB];
            spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
            spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
            spop.qpop.option = 0;
            spop.qpop.callback = load_patchdb_action;
            spop.qpop.clear_popup_ok = false;
            break;
          default:
            spop.alert_msg = msgs[lang_id][MSG_ERR_UNKTYP];
            break;
          };
        }
        f_close(&fi);
      }
    }
  }
}

static void browser_reload_filter() {
  // Instead of sorting the actual list of files, which requires moving lots
  // of memory, we use a list of pointers.
  unsigned fcount = 0;
  for (unsigned i = 0; i < smenu.browser.maxentries; i++) {
    if ((sdr_state->fentries[i].attr & AM_HID) && hide_hidden)
      continue;

    sdr_state->fileorder[fcount++] = &sdr_state->fentries[i];
  }

  heapsort4(sdr_state->fileorder, fcount, sizeof(t_centry*) / sizeof(uint32_t), filesort);

  if (smenu.browser.selector >= fcount)
    smenu.browser.selector = fcount - 1;
  smenu.browser.seloff = MAX(0, smenu.browser.selector - BROWSER_ROWS / 2);
  smenu.browser.dispentries = fcount;
}

// Loads a new directory list in the ROM browser.
// TODO: Implement filtering (.gba/.rom/.bin... etc) using settings
static void browser_reload() {
  smenu.anim_state = 0;

  unsigned fcount = 0;
  DIR d;
  if (FR_OK != f_opendir(&d, smenu.browser.cpath))
    return;   // FIXME: Implement error reporting!

  while (1) {
    FILINFO info;
    if (f_readdir(&d, &info) != FR_OK || !info.fname[0])
      break;

    if (fcount >= BROWSER_MAXFN_CNT)
      break;

    t_centry *e = &sdr_state->fentries[fcount++];
    e->filesize = (uint32_t) info.fsize;  // TODO: Support 4GB+ files?
    e->isdir = (info.fattrib & AM_DIR) ? 1 : 0;
    e->attr = info.fattrib;
    dma_memcpy16(e->fname, info.fname, MAX_FN_LEN/2);
    sortable_utf8_u16(info.fname, e->sortname);
  }
  smenu.browser.maxentries = fcount;

  // Filter and sort list of files/dirs
  browser_reload_filter();
}

// Loads NOR game entries so they can be browsed.
static void flashbrowser_reload() {
  #ifdef SUPPORT_NORGAMES
  smenu.fbrowser.selector = 0;
  smenu.anim_state = 0;

  if (!flashmgr_load(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
    // No data found, reset the entries
    memset(&sdr_state->nordata, 0, sizeof(sdr_state->nordata));

  // Calculate block usage, free space, etc.
  smenu.fbrowser.usedblks = 0;
  for (unsigned i = 0; i < sdr_state->nordata.gamecnt; i++) {
    const t_flash_game_entry *e = &sdr_state->nordata.games[i];
    for (unsigned j = 0; j < MAX_GAME_BLOCKS; j++)
      if (e->blkmap[j])
        smenu.fbrowser.usedblks++;
  }
  smenu.fbrowser.freeblks = NOR_GAMEBLOCK_COUNT - smenu.fbrowser.usedblks;

  smenu.fbrowser.maxentries = sdr_state->nordata.gamecnt;
  heapsort4(sdr_state->nordata.games, smenu.fbrowser.maxentries, sizeof(t_flash_game_entry) / sizeof(uint32_t), romsort);
  #endif
}

// Tabs without the info bar are drawn shifted up (see menu_render): sprites
// have to follow that shift.
static int ui_yshift;

static inline void render_icon(unsigned x, unsigned y, unsigned iconn) {
  fobjs[objnum++] = (t_oamobj){x, (y - ui_yshift) & 0xFF, 8*iconn };
}

static inline void render_icon_trans(unsigned x, unsigned y, unsigned iconn) {
  fobjs[objnum++] = (t_oamobj){x, ((y - ui_yshift) & 0xFF) | 0x0400, 8*iconn };
}

// Guess the file type based on the file name.
static unsigned guessicon(const char *path) {
  const char *ext = find_extension(path);

  if (ext) {
    static const struct {
      const char *ext;
      unsigned icon;
    } exticon[] = {
      {"gba", ICON_GBACART},
      {"gb",  ICON_GBCART},
      {"gbc", ICON_GBCCART},
      {"nes", ICON_NESCART},
      {"sms", ICON_SMSCART},
      {"fw",  ICON_UPDFILE},
    };
    for (unsigned i = 0; i < sizeof(exticon)/sizeof(exticon[0]); i++)
      if (!strcasecmp(exticon[i].ext, ext))
        return exticon[i].icon;
  }

  return ICON_BINFILE;
}

// Draws text adding some support for overflow.
#define THREEDOTS_WIDTH  9
static void draw_text_ovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  else {
    char tmpbuf[256];
    unsigned numchars = font_width_cap(t, maxw - THREEDOTS_WIDTH);
    memcpy(tmpbuf, t, numchars);
    memcpy(&tmpbuf[numchars], "...", 4);
    draw_text_idx8_bus16(tmpbuf, basept, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_text_leftovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned numchars = font_width_lcap(t, maxw - THREEDOTS_WIDTH);
  if (numchars) {
    draw_text_idx8_bus16("...", basept, SCREEN_WIDTH, FT_COLOR);
    draw_text_idx8_bus16(&t[numchars], basept + THREEDOTS_WIDTH, SCREEN_WIDTH, FT_COLOR);
  } else {
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_text_ovf_rotate(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw, unsigned *franim) {
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x];
  unsigned twidth = font_width(t);
  if (twidth <= maxw)
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  else {
    unsigned anim = *franim > ANIM_INITIAL_WAIT ? (*franim - ANIM_INITIAL_WAIT) >> 4 : 0;

    // Wrap around once the text end reaches the mid point aprox.
    char tmpbuf[540];
    strcpy(tmpbuf, t);
    strcat(tmpbuf, "      ");
    unsigned pixw = font_width(tmpbuf);
    if (anim > pixw)
      *franim = ANIM_INITIAL_WAIT + ((anim - pixw) << 4);
    strcat(tmpbuf, t);

    draw_text_idx8_bus16_range(tmpbuf, basept, anim, maxw, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_box_outline(volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom, uint8_t color) {
  dma_memset16(&frame[SCREEN_WIDTH * top + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (top + 1) + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (bottom - 1) + left], dup8(color), (right - left) / 2);
  dma_memset16(&frame[SCREEN_WIDTH * (bottom - 2) + left], dup8(color), (right - left) / 2);
  while (top < bottom) {
    *((uint16_t*)&frame[SCREEN_WIDTH * top + left]) = dup8(color);
    *((uint16_t*)&frame[SCREEN_WIDTH * top + right - 2]) = dup8(color);
    top++;
  }
}

static void draw_box_full(
  volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom,
  uint8_t outlinecolor, uint8_t bgcolor
) {
  draw_box_outline(frame, left, right, top, bottom, outlinecolor);
  for (unsigned i = top + 2; i < bottom - 2; i++)
    dma_memset16(&frame[SCREEN_WIDTH * i + left + 2], dup8(bgcolor), (right - left - 4) / 2);
}

static void draw_button_box(
  volatile uint8_t *frame, unsigned left, unsigned right, unsigned top, unsigned bottom, bool selected
) {
  if (selected)
    draw_box_full(frame, left, right, top, bottom, FG_COLOR, HI_COLOR);
  else
    draw_box_outline(frame, left, right, top, bottom, FG_COLOR);
}


static void draw_rightj_text(const char *t, volatile uint8_t *frame, unsigned x, unsigned y) {
  unsigned twidth = font_width(t);
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
}

static void draw_central_text(const char *t, volatile uint8_t *frame, unsigned x, unsigned y) {
  unsigned twidth = font_width(t);
  uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth / 2];
  draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
}

static void draw_central_text_ovf(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  unsigned twidth = font_width(t);
  if (twidth <= maxw) {
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - twidth / 2];
    draw_text_idx8_bus16(t, basept, SCREEN_WIDTH, FT_COLOR);
  } else {
    char tmpbuf[256];
    unsigned numchars = font_width_cap(t, maxw - THREEDOTS_WIDTH);
    memcpy(tmpbuf, t, numchars);
    memcpy(&tmpbuf[numchars], "...", 4);
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - maxw / 2];
    draw_text_idx8_bus16(tmpbuf, basept, SCREEN_WIDTH, FT_COLOR);
  }
}

static void draw_central_text_wrapped(const char *t, volatile uint8_t *frame, unsigned x, unsigned y, unsigned maxw) {
  while (*t) {
    char tmp[128];
    unsigned outw;
    unsigned linechars = font_width_cap_space(t, maxw, &outw);
    unsigned charcnt = linechars ?: utf8_strlen(t);
    uint8_t *basept = (uint8_t*)&frame[y * SCREEN_WIDTH + x - outw / 2];

    memcpy(tmp, t, charcnt);
    tmp[charcnt] = 0;
    draw_text_idx8_bus16(tmp, basept, SCREEN_WIDTH, FT_COLOR);

    t += charcnt;      // Advance text
    y += 16;           // Move down in the buffer
  }
}

void render_recent(volatile uint8_t *frame) {
  // Render the list from memory.
  for (unsigned i = 0; i < RECENT_ROWS; i++) {
    if (smenu.recent.seloff + i >= smenu.recent.maxentries)
      break;

    t_rentry *e = &sdr_state->rentries[smenu.recent.seloff + i];
    char *fn = &e->fpath[e->fname_offset];
    render_icon(2, (i+1)*16, guessicon(fn));

    // Animate the row entries if they are too long!
    if (i == smenu.recent.selector - smenu.recent.seloff)
      draw_text_ovf_rotate(fn, frame, 20, (1 + i) * 16,
                           SCREEN_WIDTH - 24, &smenu.anim_state);
    else
      draw_text_ovf(fn, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 24);
  }

  for (unsigned i = 0; i < 240; i += 16)
    render_icon_trans(i, (smenu.recent.selector - smenu.recent.seloff + 1)*16, 63);
}

#ifdef SUPPORT_NORGAMES
void render_flashbrowser(volatile uint8_t *frame) {
  // Render bar below to show block info (the tab is drawn 16 rows up, no info bar)
  dma_memset16(&frame[240*160], dup8(FG_COLOR), 240*16/2);

  // Render the list from memory.
  if (!smenu.fbrowser.maxentries)
    draw_central_text(msgs[lang_id][MSG_NOR_EMPTY], frame, SCREEN_WIDTH/2, SCREEN_HEIGHT/2-8);
  else {
    for (unsigned i = 0; i < NORGAMES_ROWS; i++) {
      if (smenu.fbrowser.seloff + i >= smenu.fbrowser.maxentries)
        break;

      const t_flash_game_entry *e = &sdr_state->nordata.games[smenu.fbrowser.seloff + i];
      render_icon(2, (i+1)*16, ICON_GBACART);

      // Animate the row entries if they are too long!
      char szstr[16];
      human_size(szstr, sizeof(szstr), e->numblks * NOR_BLOCK_SIZE);
      draw_rightj_text(szstr, frame, SCREEN_WIDTH - 2, (1 + i) * 16);

      // Animate the row entries if they are too long!
      const char *romname = &e->game_name[e->bnoffset];
      if (i == smenu.fbrowser.selector - smenu.fbrowser.seloff)
        draw_text_ovf_rotate(romname, frame, 20, (1 + i) * 16,
                             SCREEN_WIDTH - 26 - font_width(szstr), &smenu.anim_state);
      else
        draw_text_ovf(romname, frame, 20, (1 + i) * 16, SCREEN_WIDTH - 26 - font_width(szstr));
    }

    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, (smenu.fbrowser.selector - smenu.fbrowser.seloff + 1)*16, 63);
  }

  char tmp[32], tmp1[32], tmp2[32];
  npf_snprintf(tmp, sizeof(tmp), "%u/%d", smenu.fbrowser.selector + 1, smenu.fbrowser.maxentries);
  draw_rightj_text(tmp, frame, SCREEN_WIDTH - 1, 160);

  human_size(tmp1, sizeof(tmp1), smenu.fbrowser.usedblks * NOR_BLOCK_SIZE);
  human_size(tmp2, sizeof(tmp2), NOR_GAMEBLOCK_COUNT * NOR_BLOCK_SIZE);
  npf_snprintf(tmp, sizeof(tmp), "Flash usage: %s/%s", tmp1, tmp2);
  draw_text_ovf(tmp, frame, 8, 160, SCREEN_WIDTH - 56);
}
#endif

// ---------------------------------------------------------------------------
// DS-style views
//
// The ROM browser tab is a horizontally scrolling carousel with one plate per
// entry of the current directory (the selection lives in smenu.browser, so the
// file manager pop up, hiding files, etc. keep working). The other DS-style
// view is the top-screen clock with a month calendar. Everything is drawn with
// plain shapes and text (no bitmaps, to save memory): the icons are outlines.
// ---------------------------------------------------------------------------

static unsigned render_fcnt;     // Frames elapsed since the previous render
static int car_pos;              // Scroll position in 1/256 of an entry

// Fills the horizontal span [x0, x1) of row y. VRAM only accepts 16 bit writes
// in this video mode, so unaligned edges need a read-modify-write.
static void car_span(volatile uint8_t *frame, int y, int x0, int x1, uint8_t color) {
  if (x0 < 0)
    x0 = 0;
  if (x1 > SCREEN_WIDTH)
    x1 = SCREEN_WIDTH;
  if (x0 >= x1)
    return;

  volatile uint16_t *row = (volatile uint16_t*)&frame[y * SCREEN_WIDTH];
  if (x0 & 1) {
    row[x0 >> 1] = (row[x0 >> 1] & 0x00FF) | ((uint16_t)color << 8);
    x0++;
  }
  if (x0 >= x1)
    return;
  if (x1 & 1) {
    x1--;
    row[x1 >> 1] = (row[x1 >> 1] & 0xFF00) | color;
  }
  if (x0 < x1)
    dma_memset16(&frame[y * SCREEN_WIDTH + x0], dup8(color), (x1 - x0) / 2);
}

// Fills the rectangle [x0, x1) x [y0, y1), clipped to the screen.
static void car_rect(volatile uint8_t *frame, int x0, int y0, int x1, int y1, uint8_t color) {
  // Rows are relative to the (possibly shifted up, see ui_yshift) frame pointer.
  if (y0 < ui_yshift)
    y0 = ui_yshift;
  if (y1 > SCREEN_HEIGHT + ui_yshift)
    y1 = SCREEN_HEIGHT + ui_yshift;
  for (int y = y0; y < y1; y++)
    car_span(frame, y, x0, x1, color);
}

static void car_outline(volatile uint8_t *frame, int x0, int y0, int x1, int y1, int th, uint8_t color) {
  car_rect(frame, x0, y0, x1, y0 + th, color);
  car_rect(frame, x0, y1 - th, x1, y1, color);
  car_rect(frame, x0, y0 + th, x0 + th, y1 - th, color);
  car_rect(frame, x1 - th, y0 + th, x1, y1 - th, color);
}

// Moves the scroll position towards the selected entry. Takes a fraction of
// the remaining distance each frame and snaps once it is close enough.
static void car_ease() {
  const int dest = smenu.browser.selector << 8;
  unsigned frames = render_fcnt > 8 ? 8 : render_fcnt;
  while (frames--) {
    const int diff = dest - car_pos;
    const int step = diff / CAR_SPEED_DIV;
    car_pos += step ? step : diff;
  }
}

// Screen X of the center of entry i, for the given spacing between entries.
static int car_xof(int i, int pitch) {
  return SCREEN_WIDTH / 2 + ((((i << 8) - car_pos) * pitch) >> 8);
}

// Icons of the file lists, drawn with plain shapes at any size: a folder, a
// generic file and a game (any ROM the menu can launch).
enum { ICON_KIND_FOLDER, ICON_KIND_FILE, ICON_KIND_GAME };

static unsigned entry_icon_kind(const t_centry *e) {
  if (e->isdir)
    return ICON_KIND_FOLDER;
  const char *ext = find_extension(e->fname);
  if (ext) {
    static const char *const gameext[] = { "gba", "gb", "gbc", "nes", "sms" };
    for (unsigned i = 0; i < sizeof(gameext) / sizeof(gameext[0]); i++)
      if (!strcasecmp(ext, gameext[i]))
        return ICON_KIND_GAME;
  }
  return ICON_KIND_FILE;
}

// Draws an icon of size sz (even) centered horizontally at cx, top at y.
// Only outlines are drawn, so whatever is below shows through.
static void car_icon(volatile uint8_t *frame, int cx, int y, int sz, unsigned kind) {
  const int x = cx - sz / 2, u = sz / 8;
  switch (kind) {
  case ICON_KIND_FOLDER:
    car_rect(frame, x, y + u, x + 3 * u, y + 2 * u + 2, FT_COLOR);           // Tab
    car_outline(frame, x, y + 2 * u, x + sz, y + sz - u, 2, FT_COLOR);       // Body
    car_rect(frame, x + 2, y + 3 * u, x + sz - 2, y + 3 * u + 2, FT_COLOR);  // Open flap edge
    break;
  case ICON_KIND_FILE: {
    const int x0 = x + u, x1 = x + sz - u, fold = 2 * u;
    car_rect(frame, x0, y, x1 - fold, y + 2, FT_COLOR);                      // Page outline, top right corner folded
    car_rect(frame, x0, y, x0 + 2, y + sz, FT_COLOR);
    car_rect(frame, x0, y + sz - 2, x1, y + sz, FT_COLOR);
    car_rect(frame, x1 - 2, y + fold, x1, y + sz, FT_COLOR);
    car_rect(frame, x1 - fold, y, x1 - fold + 2, y + fold, FT_COLOR);        // Fold
    car_rect(frame, x1 - fold, y + fold - 2, x1, y + fold, FT_COLOR);
    for (int i = 0; i < 3; i++)                                              // Text lines
      car_rect(frame, x0 + 2 + u, y + 3 * u + i * (2 * u - 1), x1 - 2 - u, y + 3 * u + i * (2 * u - 1) + 2, FT_COLOR);
    break; }
  default:
    car_rect(frame, x + u, y + u, x + sz - u, y + 3 * u, FT_COLOR);          // Cartridge: notch, body outline, label outline
    car_outline(frame, x, y + 2 * u, x + sz, y + sz - u, 2, FT_COLOR);
    car_outline(frame, x + u + 2, y + 3 * u + 2, x + sz - u - 2, y + 6 * u - 1, 1, FT_COLOR);
    car_rect(frame, x + 2 * u, y + sz - 2 * u, x + sz - 2 * u, y + sz - u - 1, FT_COLOR);   // Contacts
    break;
  }
}

// ---------------------------------------------------------------------------
// Carousel, in the style of the DSi menu (colors of the DSi theme of TWiLight
// Menu++): a speech bubble with the name of the selected entry, a row of white
// plates with the entry icons that slides under a fixed blue slot holding the
// selected one (labeled START), and a scroll bar with arrow buttons, a dot
// track and a thumb at the bottom. Everything is drawn with spans, no textures.
// ---------------------------------------------------------------------------

// Text of a given color, centered at x.
static void text_center(volatile uint8_t *frame, const char *t, int cx, int y, uint8_t color) {
  draw_text_idx8_bus16(t, (uint8_t*)&frame[y * SCREEN_WIDTH + cx - (int)font_width(t) / 2], SCREEN_WIDTH, color);
}

// Rounded rectangle, corner radius r (0..4) in pixels. The fill uses n palette
// colors starting at base, from the top row to the bottom row.
static void ds_rrect(volatile uint8_t *frame, int x0, int y0, int x1, int y1, int r, uint8_t base, int n) {
  static const uint8_t inset[5][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 2, 1, 0, 0 }, { 3, 1, 1, 0 }, { 4, 2, 1, 1 } };
  const int h = y1 - y0;
  for (int y = y0; y < y1; y++) {
    const int d = MIN(y - y0, y1 - 1 - y);
    const int i = d < 4 ? inset[r][d] : 0;
    car_span(frame, y, x0 + i, x1 - i, base + (h > 1 ? (y - y0) * (n - 1) / (h - 1) : 0));
  }
}

// White plate of an entry: gradient face and a darker lip, 48x48, top at y.
static void car_plate(volatile uint8_t *frame, int cx, int y) {
  const int x0 = cx - CAR_BOX_W / 2;
  ds_rrect(frame, x0, y, x0 + CAR_BOX_W, y + CAR_BOX_H, 4, PL_EDGE, 1);
  ds_rrect(frame, x0 + 1, y + 1, x0 + CAR_BOX_W - 1, y + CAR_BOX_H - 4, 3, PL_BASE, 6);
  ds_rrect(frame, x0 + 2, y + CAR_BOX_H - 4, x0 + CAR_BOX_W - 2, y + CAR_BOX_H - 1, 1, PL_SHADE, 1);
}

// Arrow button of the scroll bar (square, 16x16). Dimmed (flat gray) when it can't move.
static void car_arrow(volatile uint8_t *frame, int x0, bool left, bool enabled) {
  const int y0 = CAR_BAR_Y;
  ds_rrect(frame, x0, y0, x0 + 16, y0 + CAR_BAR_H, 0, enabled ? BL_EDGE : PL_EDGE, 1);
  if (enabled)
    ds_rrect(frame, x0 + 1, y0 + 1, x0 + 15, y0 + CAR_BAR_H - 1, 0, BL_BASE, 6);
  else
    ds_rrect(frame, x0 + 1, y0 + 1, x0 + 15, y0 + CAR_BAR_H - 1, 0, PL_SHADE, 1);
  const int tip = left ? x0 + 4 : x0 + 12, base = left ? x0 + 10 : x0 + 6;
  for (int dy = -4; dy <= 4; dy++) {
    const int w = dy < 0 ? -dy : dy;
    if (left)
      car_span(frame, y0 + 8 + dy, tip + w, base + 1, BAR_WHITE);
    else
      car_span(frame, y0 + 8 + dy, base, tip + 1 - w, BAR_WHITE);
  }
}

void render_carousel(volatile uint8_t *frame) {
  const int n = smenu.browser.dispentries;
  const int cx = SCREEN_WIDTH / 2;
  car_ease();

  // Speech bubble with a pointer to the selected plate: flat white face, double
  // frame (darker gray outside, lighter gray inside).
  ds_rrect(frame, 8, CAR_BUB_Y, SCREEN_WIDTH - 8, CAR_BUB_Y + CAR_BUB_H, 4, PL_EDGE, 1);
  ds_rrect(frame, 9, CAR_BUB_Y + 1, SCREEN_WIDTH - 9, CAR_BUB_Y + CAR_BUB_H - 1, 3, PL_BASE + 4, 1);
  ds_rrect(frame, 10, CAR_BUB_Y + 2, SCREEN_WIDTH - 10, CAR_BUB_Y + CAR_BUB_H - 2, 2, PL_BASE, 1);
  for (int r = -1; r < 7; r++) {
    const int w = 7 - r, y = CAR_BUB_Y + CAR_BUB_H + r;
    car_span(frame, y, cx - w, cx + w + 1, PL_EDGE);
    car_span(frame, y, cx - w + 1, cx + w, PL_BASE + 4);
    car_span(frame, y, cx - w + 2, cx + w - 1, PL_BASE);
  }

  if (!n) {
    draw_central_text(msgs[lang_id][MSG_BROW_EMPTY], frame, cx, CAR_BUB_Y + 14);
  } else {
    // Name and, below it, the size and the current directory. Entry counter at the right.
    const t_centry *e = sdr_state->fileorder[smenu.browser.selector];
    char info[MAX_FN_LEN + 16];
    if (e->isdir) {
      npf_snprintf(info, sizeof(info), "Folder  %s", smenu.browser.cpath);
    } else {
      char szstr[16] = {0};
      human_size(szstr, sizeof(szstr), e->filesize);
      npf_snprintf(info, sizeof(info), "%s  %s", szstr, smenu.browser.cpath);
    }
    draw_central_text_ovf(e->fname, frame, cx, CAR_BUB_Y + 4, 150);
    draw_central_text_ovf(info, frame, cx, CAR_BUB_Y + 22, SCREEN_WIDTH - 32);
    char cnt[16];
    npf_snprintf(cnt, sizeof(cnt), "%d/%d", smenu.browser.selector + 1, n);
    draw_rightj_text(cnt, frame, SCREEN_WIDTH - 14, CAR_BUB_Y + 4);
  }

  // The blue slot of the selected entry, under the plates that scroll through it.
  ds_rrect(frame, cx - CAR_SLOT_W / 2, CAR_SLOT_Y, cx + CAR_SLOT_W / 2, CAR_SLOT_Y + CAR_SLOT_H, 4, BL_EDGE, 1);
  ds_rrect(frame, cx - CAR_SLOT_W / 2 + 1, CAR_SLOT_Y + 1, cx + CAR_SLOT_W / 2 - 1, CAR_SLOT_Y + CAR_SLOT_H - 1, 3, BL_BASE, 6);

  if (n) {
    // Draw only the plates that are (at least partially) on screen.
    const int range = cx / CAR_PITCH + 2;
    const int first = MAX(0, (car_pos >> 8) - range);
    const int last = MIN(n - 1, (car_pos >> 8) + range);
    for (int i = first; i <= last; i++) {
      const int bx = car_xof(i, CAR_PITCH);
      car_plate(frame, bx, CAR_PLATE_Y);
      car_icon(frame, bx, CAR_PLATE_Y + CAR_ICON_Y, CAR_ICON_SZ, entry_icon_kind(sdr_state->fileorder[i]));
    }
    text_center(frame, sdr_state->fileorder[smenu.browser.selector]->isdir ? "OPEN" : "START",
                cx, CAR_PLATE_Y + CAR_BOX_H - 2, BAR_WHITE);
  }

  // Scroll bar: arrow buttons, a track of dots and the thumb, all square and
  // touching each other along the bottom of the screen.
  const int sel = smenu.browser.selector;
  car_arrow(frame, 0, true, n && sel > 0);
  car_arrow(frame, SCREEN_WIDTH - 16, false, n && sel < n - 1);
  ds_rrect(frame, CAR_TRK_X0, CAR_BAR_Y, CAR_TRK_X1, CAR_BAR_Y + CAR_BAR_H, 0, PL_EDGE, 1);
  ds_rrect(frame, CAR_TRK_X0, CAR_BAR_Y + 1, CAR_TRK_X1, CAR_BAR_Y + CAR_BAR_H - 1, 0, PL_BASE + 2, 4);
  const int span = CAR_TRK_X1 - CAR_TRK_X0 - 2 - CAR_THUMB;
  int thumb = CAR_TRK_X0 + 1;
  if (n > 1) {
    const int pos = MAX(0, MIN(car_pos, (n - 1) << 8));
    thumb += (pos >> 4) * span / (((n - 1) << 8) >> 4);
  }
  for (int x = CAR_TRK_X0 + 5; x + 3 <= CAR_TRK_X1 - 4; x += 4) {
    if (x < thumb + CAR_THUMB / 2)
      car_rect(frame, x, CAR_BAR_Y + 6, x + 3, CAR_BAR_Y + 9, GR_DOT);
    else
      car_rect(frame, x + 1, CAR_BAR_Y + 7, x + 3, CAR_BAR_Y + 9, PL_EDGE);
  }
  ds_rrect(frame, thumb, CAR_BAR_Y, thumb + CAR_THUMB, CAR_BAR_Y + CAR_BAR_H, 0, BL_EDGE, 1);
  ds_rrect(frame, thumb + 1, CAR_BAR_Y + 1, thumb + CAR_THUMB - 1, CAR_BAR_Y + CAR_BAR_H - 1, 0, PL_BASE, 6);
  car_rect(frame, thumb + CAR_THUMB / 2 - 2, CAR_BAR_Y + CAR_BAR_H / 2 - 2, thumb + CAR_THUMB / 2 + 2, CAR_BAR_Y + CAR_BAR_H / 2 + 2, BL_EDGE);
}

// ---------------------------------------------------------------------------
// Clock and calendar (top screen of the DS menus).
//
// The Supercard has no real time clock, so the clock is kept in software: a
// base time (set by the user, stored in the UI settings) plus the time elapsed
// since it was set, counted in video frames. It stops advancing while
// interrupts are masked and it has to be set again after a power cycle.
// ---------------------------------------------------------------------------

static uint32_t clock_base_ts;       // Seconds since 2000-01-01 at clock_base_frame
static unsigned clock_base_frame;    // Value of frame_count when it was set

static uint32_t clock_now() {
  // 280896 cycles per frame at 16.78MHz: 4389 / 2^18 seconds per frame.
  const uint32_t elapsed = (uint32_t)(((uint64_t)(frame_count - clock_base_frame) * 4389) >> 18);
  return clock_base_ts + elapsed;
}

static void clock_set_callback() {
  clock_base_ts = date2timestamp(&spop.rtcpop.val);
  clock_base_frame = frame_count;
  clock_ts_default = clock_base_ts;
  save_ui_settings();
}

static void clock_open_setter() {
  timestamp2date(clock_now(), &spop.rtcpop.val);
  spop.rtcpop.selector = 0;
  spop.rtcpop.title = "Set clock";
  spop.rtcpop.callback = clock_set_callback;
}

static const char *const clk_wdays_short[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

// Number of days in a month (year is 0..99, years since 2000: all multiples
// of four are leap years).
static unsigned clock_dim(unsigned year, unsigned month) {
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return dim[month - 1] + (month == 2 && (year % 4) == 0);
}

// Day of the week, 0 is Sunday. 2000-01-01 was a Saturday.
static unsigned clock_wday(uint32_t ts) {
  return (6 + ts / 86400) % 7;
}

// ---------------------------------------------------------------------------
// Background shared by the home and clock tabs: a square grid on rows that
// alternate between two close colors, to get faint scanlines like the DS menu.
// The rows are prebuilt (palette index based, so they never change).
// ---------------------------------------------------------------------------
// Grid sizes (tile width, height) of each background; row 0 is the clock
// tab (16x16 tiles) and row 1 the home tab (48x48 tiles).
enum { BG_CLOCK = 0, BG_HOME = 1, BG_COUNT };
static const uint8_t bg_tiles[BG_COUNT][2] = { { 16, 16 }, { 48, 48 } };
// Grid lines sit at x + offset multiple of the tile width: the clock tab grid is
// shifted 8 pixels left and the home grid 24 pixels left.
static const uint8_t bg_xoff[BG_COUNT] = { 8, 24 };
static const uint8_t bg_yoff[BG_COUNT] = { 8, 0 };     // The clock tab grid is also shifted 8 pixels down
static uint16_t bg_rows[BG_COUNT][4][SCREEN_WIDTH / 2];   // Bit 0: odd row, bit 1: grid line

static void bg_init(void) {
  for (unsigned g = 0; g < BG_COUNT; g++)
    for (unsigned t = 0; t < 4; t++) {
      uint8_t *row = (uint8_t*)bg_rows[g][t];
      const uint8_t bg = (t & 1) ? SL_BG : BG_COLOR;
      const uint8_t ln = (t & 1) ? SL_GRID : GRID_COLOR;
      for (unsigned x = 0; x < SCREEN_WIDTH; x++)
        row[x] = ((t & 2) || ((x + bg_xoff[g]) % bg_tiles[g][0]) == 0) ? ln : bg;
    }
}

// Draws the background below the info bar: square tiles on rows that
// alternate between two close colors, to get faint scanlines like the DS.
static void ds_background(volatile uint8_t *frame, unsigned grid) {
  const unsigned th = bg_tiles[grid][1];
  for (int y = 16; y < SCREEN_HEIGHT; y++)
    dma_memcpy16(&frame[y * SCREEN_WIDTH], bg_rows[grid][(((y - 16 + bg_yoff[grid]) % th) ? 0 : 2) | (y & 1)], SCREEN_WIDTH / 2);
}


// Rectangle with rounded corners (3 pixel profile).
static void car_rrect(volatile uint8_t *frame, int x0, int y0, int x1, int y1, uint8_t color) {
  static const uint8_t inset[3] = { 3, 1, 1 };
  for (int y = y0; y < y1; y++) {
    const int d = MIN(y - y0, y1 - 1 - y);
    const int i = d < 3 ? inset[d] : 0;
    car_span(frame, y, x0 + i, x1 - i, color);
  }
}

// Sine table, 256 * sin(deg) for 0..90 degrees.
static const uint16_t sin_lut[91] = {
  0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 58, 62, 66, 71, 75, 79, 83, 88, 92, 96, 100, 104,
  108, 112, 116, 120, 124, 128, 132, 136, 139, 143, 147, 150, 154, 158, 161, 165, 168, 171, 175, 178,
  181, 184, 187, 190, 193, 196, 199, 202, 204, 207, 210, 212, 215, 217, 219, 222, 224, 226, 228, 230,
  232, 234, 236, 237, 239, 241, 242, 243, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 254, 255,
  255, 255, 256, 256, 256, 256,
};

static int isin(int deg) {
  deg %= 360;
  if (deg < 0)
    deg += 360;
  if (deg <= 90)  return sin_lut[deg];
  if (deg <= 180) return sin_lut[180 - deg];
  if (deg <= 270) return -sin_lut[deg - 180];
  return -sin_lut[360 - deg];
}

// Hand tip offset for an angle (degrees clockwise from 12) and a length.
static int clock_off(int v) {
  return (v + (v < 0 ? -128 : 128)) / 256;
}

// Draws a clock hand, a 2 pixel thick line from the pin. This is the needle
// code of TWiLight Menu++ (quickmenu theme) with integer trigonometry.
static void clock_needle(volatile uint8_t *frame, int angle, int length, uint8_t color) {
  int x0 = CLK_CX, y0 = CLK_CY;
  const int x1 = x0 + clock_off(length * isin(angle));
  const int y1 = y0 - clock_off(length * isin(angle + 90));

  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
  const int stepx = x0 < x1 ? 1 : -1;
  const int stepy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (1) {
    car_rect(frame, x0 - 1, y0, x0 + 1, y0 + 2, color);
    if (x0 == x1 && y0 == y1)
      break;
    const int error2 = error * 2;
    if (error2 >= dy) {
      if (x0 == x1)
        break;
      error += dy;
      x0 += stepx;
    }
    if (error2 <= dx) {
      if (y0 == y1)
        break;
      error += dx;
      y0 += stepy;
    }
  }
}

// Clock face, with the look of the TWiLight Menu++ clock graphic: the grid
// shows through, the dots sit at the corner tiles and the numbers 12, 3, 6 and 9
// are the same blocky digits. Those are not a font: they are baked into its
// clock.png, so the four glyphs are traced here, as 5 bands of 4 pixel rows.
static const char *const clock_glyphs[4][5] = {
  { "####.###########", "####........####", "####.###########", "####.####.......", "####.###########" },   // 12
  { "############", "........####", "############", "........####", "############" },                       // 3
  { "############", "####........", "############", "####....####", "############" },                       // 6
  { "############", "####....####", "############", "........####", "############" },                       // 9
};

static void clock_glyph(volatile uint8_t *frame, int x, int y, unsigned g) {
  for (int b = 0; b < 5; b++) {
    const char *row = clock_glyphs[g][b];
    for (int c = 0; row[c]; ) {
      if (row[c] != '#') { c++; continue; }
      int e = c;
      while (row[e] == '#')
        e++;
      car_rect(frame, x + c, y + b * 4, x + e, y + b * 4 + 4, CLK_TICK);
      c = e;
    }
  }
}

static void clock_face(volatile uint8_t *frame) {
  car_rect(frame, CLK_FACE_X, CLK_FACE_Y, CLK_FACE_X + CLK_FACE_SZ, CLK_FACE_Y + CLK_FACE_SZ, 0);     // Black square frame
  car_rect(frame, CLK_FACE_X + 2, CLK_FACE_Y + 2, CLK_FACE_X + CLK_FACE_SZ - 2, CLK_FACE_Y + CLK_FACE_SZ - 2, CAL_BG);

  // The background grid, continued over the face (solid lines, no scanlines).
  for (int i = 1; i < CLK_FACE_SZ / 16; i++) {
    car_rect(frame, CLK_FACE_X + i * 16, CLK_FACE_Y + 2, CLK_FACE_X + i * 16 + 1, CLK_FACE_Y + CLK_FACE_SZ - 2, CLK_FGRID);
    car_span(frame, CLK_FACE_Y + i * 16, CLK_FACE_X + 2, CLK_FACE_X + CLK_FACE_SZ - 2, CLK_FGRID);
  }

  // Dots (4x4) at the tiles next to the corners, and the numbers on the axes.
  static const int8_t dpos[8][2] = {
    { -22, -37 }, { 22, -37 }, { -37, -22 }, { 37, -22 }, { -37, 22 }, { 37, 22 }, { -22, 37 }, { 22, 37 },
  };
  for (int i = 0; i < 8; i++)
    car_rect(frame, CLK_CX + dpos[i][0] - 2, CLK_CY + dpos[i][1] - 2, CLK_CX + dpos[i][0] + 2, CLK_CY + dpos[i][1] + 2, CLK_HAND);

  clock_glyph(frame, CLK_CX - 8,  CLK_CY - 44, 0);
  clock_glyph(frame, CLK_CX + 32, CLK_CY - 10, 1);
  clock_glyph(frame, CLK_CX - 6,  CLK_CY + 24, 2);
  clock_glyph(frame, CLK_CX - 44, CLK_CY - 10, 3);
}

void render_clock(volatile uint8_t *frame) {
  ds_background(frame, BG_CLOCK);

  const uint32_t now = clock_now();
  t_dec_date d;
  timestamp2date(now, &d);
  const unsigned year = d.year, month = d.month, day = d.day;
  char buf[24];

  // Analog clock: hour and minute hands in grey, seconds in the accent color.
  clock_face(frame);
  clock_needle(frame, (d.hour % 12) * 30 + d.min / 2, 24, CLK_HAND);
  clock_needle(frame, d.min * 6, 30, CLK_HAND);
  clock_needle(frame, d.sec * 6, 36, ACC_COLOR);
  car_rect(frame, CLK_CX - 2, CLK_CY - 2, CLK_CX + 3, CLK_CY + 3, CLK_PIN);
  text_center(frame, "A: Set time", CLK_FACE_X + CLK_FACE_SZ / 2, 140, FT_COLOR);     // Below the face, where the footer bar was

  // Calendar, on the tile grid: a title row, a weekday header row and as many
  // week rows as the month needs. Sunday and Saturday columns are tinted.
  const unsigned first = clock_wday(date2timestamp(&(t_dec_date){ .year = d.year, .month = d.month, .day = 1 }));
  const unsigned weeks = (first + clock_dim(year, month) + 6) / 7;
  const int bottom = CAL_Y + (2 + weeks) * CAL_TILE;
  car_rect(frame, CAL_X, CAL_Y, CAL_X + 7 * CAL_TILE, bottom, CAL_BG);
  car_rect(frame, CAL_X, CAL_Y + 32, CAL_X + CAL_TILE, bottom, CAL_PINK);
  car_rect(frame, CAL_X + 6 * CAL_TILE, CAL_Y + 32, CAL_X + 7 * CAL_TILE, bottom, CAL_LBLUE);
  // Weekday header cells.
  for (unsigned c = 0; c < 7; c++) {
    const int x = CAL_X + c * CAL_TILE;
    car_rect(frame, x, CAL_Y + 16, x + CAL_TILE, CAL_Y + 32, c == 0 ? CAL_HRED : c == 6 ? CAL_HBLUE : CAL_HGRAY);
  }
  // Grid lines on the same 16x16 grid as the background (solid, no scanlines),
  // drawn over the header cells too.
  for (int x = CAL_X; x < CAL_X + 7 * CAL_TILE; x += CAL_TILE)
    car_rect(frame, x, CAL_Y + 16, x + 1, bottom, CAL_LINE);
  for (int y = CAL_Y + 16; y < bottom; y += CAL_TILE)
    car_span(frame, y, CAL_X, CAL_X + 7 * CAL_TILE, CAL_LINE);
  car_outline(frame, CAL_X, CAL_Y, CAL_X + 7 * CAL_TILE, bottom, 1, CAL_LINE);

  npf_snprintf(buf, sizeof(buf), "%02d/%d", month, 2000 + year);
  text_center(frame, buf, CAL_X + CAL_TILE * 7 / 2, CAL_Y, CAL_TXT);
  for (unsigned c = 0; c < 7; c++) {
    const int x = CAL_X + c * CAL_TILE;
    text_center(frame, clk_wdays_short[c], x + CAL_TILE / 2, CAL_Y + 16, BAR_WHITE);
  }

  for (unsigned dd = 1; dd <= clock_dim(year, month); dd++) {
    const unsigned idx = first + dd - 1;
    const int x = CAL_X + (idx % 7) * CAL_TILE;
    const int y = CAL_Y + 32 + (idx / 7) * CAL_TILE;
    uint8_t color = (idx % 7) == 0 ? CAL_RED : (idx % 7) == 6 ? CAL_BLUE : CAL_TXT;
    if (dd == day) {
      car_rrect(frame, x + 1, y + 1, x + CAL_TILE - 1, y + CAL_TILE - 1, ACC_COLOR);
      color = BAR_TX;
    }
    npf_snprintf(buf, sizeof(buf), "%u", dd);
    text_center(frame, buf, x + CAL_TILE / 2, y, color);
  }
}

// ---------------------------------------------------------------------------
// Home screen, modeled after the Nintendo DS (Phat) start menu: buttons with a
// gradient face and a label plate on a grid background, a selection marked by
// corner brackets and three small buttons at the bottom (theme, settings,
// time). Plain shapes only; the icons are outlines so the face shows through.
// ---------------------------------------------------------------------------

void reload_theme(unsigned thnum);

enum {
  HB_FLASH = 0,      // Games stored in the cart flash (fast loading ROMs)
  HB_RECENT,         // Recent games tab
  HB_HWTEST,         // Hardware test (tools) tab
  HB_CAROUSEL,       // Opens the carousel
  HB_THEME,          // Switches the light/dark theme
  HB_SETTINGS,       // Opens the settings tabs (settings, UI settings, about)
  HB_TIME,           // Sets the clock
  HB_COUNT,
};

// Buttons sit on the 48x48 tile grid of the background (lines at x = 24 + 48k,
// shifted 24 pixels left so 4 tiles are centered; y = 16 + 48k), 2 pixels in
// from the grid lines: flash games and the carousel are 4 tiles wide, recent
// games and hardware test 2 each.
static const struct { int16_t x0, y0, x1, y1; } home_rects[HB_COUNT] = {
  [HB_FLASH]    = {  26,  18, 214,  62 },
  [HB_RECENT]   = {  26,  66, 118, 110 },
  [HB_HWTEST]   = { 122,  66, 214, 110 },
  [HB_CAROUSEL] = {  26, 114, 214, 136 },
  [HB_THEME]    = {   2, 138,  22, 158 },
  [HB_SETTINGS] = {  98, 138, 142, 158 },
  [HB_TIME]     = { 218, 138, 238, 158 },
};

// Where each button leads to with up, down, left, right (-1: nowhere).
static const int8_t home_nav[HB_COUNT][4] = {
  [HB_FLASH]    = { -1,          HB_RECENT,   -1,          -1 },
  [HB_RECENT]   = { HB_FLASH,    HB_CAROUSEL, -1,          HB_HWTEST },
  [HB_HWTEST]   = { HB_FLASH,    HB_CAROUSEL, HB_RECENT,   -1 },
  [HB_CAROUSEL] = { HB_RECENT,   HB_SETTINGS, -1,          -1 },
  [HB_THEME]    = { HB_CAROUSEL, -1,          -1,          HB_SETTINGS },
  [HB_SETTINGS] = { HB_CAROUSEL, -1,          HB_THEME,    HB_TIME },
  [HB_TIME]     = { HB_CAROUSEL, -1,          HB_SETTINGS, -1 },
};

static int isqrt_floor(int v) {
  int w = 0;
  while ((w + 1) * (w + 1) <= v)
    w++;
  return w;
}

// Filled circle.
static void home_disc(volatile uint8_t *frame, int cx, int cy, int r, uint8_t color) {
  for (int dy = -r; dy <= r; dy++) {
    const int w = isqrt_floor(r * r - dy * dy);
    car_span(frame, cy + dy, cx - w, cx + w + 1, color);
  }
}

// Ring (circle with a hole, which stays transparent). Rows in [gy0, gy1) skip
// everything left of gx1, to leave a gap in the ring.
static void home_ring(volatile uint8_t *frame, int cx, int cy, int ro, int ri, uint8_t color,
                      int gx1, int gy0, int gy1) {
  for (int dy = -ro; dy <= ro; dy++) {
    const int wo = isqrt_floor(ro * ro - dy * dy);
    int segs[2][2];
    int n = 0;
    if (dy >= -ri && dy <= ri) {
      const int wi = isqrt_floor(ri * ri - dy * dy);
      segs[n][0] = cx - wo; segs[n][1] = cx - wi; n++;
      segs[n][0] = cx + wi + 1; segs[n][1] = cx + wo + 1; n++;
    } else {
      segs[n][0] = cx - wo; segs[n][1] = cx + wo + 1; n++;
    }
    for (int i = 0; i < n; i++) {
      int x0 = segs[i][0];
      if (cy + dy >= gy0 && cy + dy < gy1 && x0 < gx1)
        x0 = gx1;
      if (x0 < segs[i][1])
        car_span(frame, cy + dy, x0, segs[i][1], color);
    }
  }
}

// Circle minus another circle, to draw a crescent without painting over anything.
static void home_crescent(volatile uint8_t *frame, int cx, int cy, int r, int bx, int by, int rb, uint8_t color) {
  for (int dy = -r; dy <= r; dy++) {
    const int w = isqrt_floor(r * r - dy * dy);
    int a0 = cx - w, a1 = cx + w + 1;
    const int by_d = cy + dy - by;
    if (by_d >= -rb && by_d <= rb) {
      const int wb = isqrt_floor(rb * rb - by_d * by_d);
      const int b0 = bx - wb, b1 = bx + wb + 1;
      if (b0 > a0)
        car_span(frame, cy + dy, a0, MIN(a1, b0), color);
      if (b1 < a1)
        car_span(frame, cy + dy, MAX(a0, b1), a1, color);
    } else {
      car_span(frame, cy + dy, a0, a1, color);
    }
  }
}

// Button face: a vertical gradient with many shades, and an outline.
static void home_button(volatile uint8_t *frame, int b) {
  const int x0 = home_rects[b].x0, y0 = home_rects[b].y0, x1 = home_rects[b].x1, y1 = home_rects[b].y1;
  for (int y = y0; y < y1; y++)
    car_span(frame, y, x0, x1, GRAD_BASE + (y - y0) * (GRAD_N - 1) / (y1 - y0 - 1));
  car_outline(frame, x0, y0, x1, y1, 1, BTN_EDGE);
}

// Label plate: background color with scanlines (same row parity as the
// screen background) and a thin outline.
static void home_plate(volatile uint8_t *frame, int x0, int y0, int x1, int y1) {
  for (int y = y0; y < y1; y++)
    car_span(frame, y, x0, x1, (y & 1) ? SL_BG : BG_COLOR);
  car_outline(frame, x0, y0, x1, y1, 1, BTN_EDGE);
}

// Corner brackets around the selected button.
static void home_brackets(volatile uint8_t *frame, int b) {
  const int x0 = home_rects[b].x0 - 2, y0 = home_rects[b].y0 - 2;
  const int x1 = home_rects[b].x1 + 2, y1 = home_rects[b].y1 + 2;
  const int len = 10, th = 2;
  car_rect(frame, x0, y0, x0 + len, y0 + th, ACC_COLOR);
  car_rect(frame, x0, y0, x0 + th, y0 + len, ACC_COLOR);
  car_rect(frame, x1 - len, y0, x1, y0 + th, ACC_COLOR);
  car_rect(frame, x1 - th, y0, x1, y0 + len, ACC_COLOR);
  car_rect(frame, x0, y1 - th, x0 + len, y1, ACC_COLOR);
  car_rect(frame, x0, y1 - len, x0 + th, y1, ACC_COLOR);
  car_rect(frame, x1 - len, y1 - th, x1, y1, ACC_COLOR);
  car_rect(frame, x1 - th, y1 - len, x1, y1, ACC_COLOR);
}

// Small cartridge (24x16) for the carousel button: outlines, transparent inside.
static void home_icon_cart_small(volatile uint8_t *frame, int x, int y) {
  car_rect(frame, x + 3, y, x + 21, y + 3, FT_COLOR);
  car_outline(frame, x, y + 3, x + 24, y + 16, 2, FT_COLOR);
  car_outline(frame, x + 6, y + 7, x + 18, y + 12, 1, FT_COLOR);
}

// Recent games: a history clock (ring with a gap and an arrow head), 32x32.
static void home_icon_recent(volatile uint8_t *frame, int x, int y) {
  const int cx = x + 16, cy = y + 16;
  home_ring(frame, cx, cy, 14, 11, FT_COLOR, x + 9, y + 8, y + 15);
  car_rect(frame, x + 2, y + 12, x + 9, y + 14, FT_COLOR);        // Arrow head (pointing down)
  car_rect(frame, x + 3, y + 14, x + 8, y + 15, FT_COLOR);
  car_rect(frame, x + 4, y + 15, x + 7, y + 16, FT_COLOR);
  car_rect(frame, cx - 1, cy - 8, cx + 1, cy + 1, FT_COLOR);      // Hands
  car_rect(frame, cx - 1, cy, cx + 6, cy + 2, FT_COLOR);
}

// Hardware test: a gear (ring with teeth), sz x sz.
static void home_icon_gear(volatile uint8_t *frame, int x, int y, int sz) {
  const int cx = x + sz / 2, cy = y + sz / 2;
  const int ro = sz * 11 / 32, ri = MAX(2, sz * 5 / 32), tw = MAX(2, sz * 3 / 32), to = sz * 15 / 32;
  home_ring(frame, cx, cy, ro, ri, FT_COLOR, 0, 0, 0);
  car_rect(frame, cx - tw, cy - to, cx + tw, cy - ro + 2, FT_COLOR);     // Teeth
  car_rect(frame, cx - tw, cy + ro - 2, cx + tw, cy + to, FT_COLOR);
  car_rect(frame, cx - to, cy - tw, cx - ro + 2, cy + tw, FT_COLOR);
  car_rect(frame, cx + ro - 2, cy - tw, cx + to, cy + tw, FT_COLOR);
  const int dd = sz * 10 / 32, ds = MAX(1, sz / 16);
  for (int i = -1; i <= 1; i += 2)
    for (int j = -1; j <= 1; j += 2)
      car_rect(frame, cx + i * dd - ds, cy + j * dd - ds, cx + i * dd + ds, cy + j * dd + ds, FT_COLOR);
  home_disc(frame, cx, cy, MAX(1, sz / 16), FT_COLOR);
}

static void home_icon_chip(volatile uint8_t *frame, int x, int y) {
  for (int i = 0; i < 4; i++) {                       // Pins on both sides
    car_rect(frame, x + 4, y + 2 + i * 5, x + 8, y + 4 + i * 5, FT_COLOR);
    car_rect(frame, x + 24, y + 2 + i * 5, x + 28, y + 4 + i * 5, FT_COLOR);
  }
  car_outline(frame, x + 8, y, x + 24, y + 22, 2, FT_COLOR);
  car_outline(frame, x + 12, y + 5, x + 20, y + 17, 1, FT_COLOR);
}

// Small icons for the bottom buttons, centered at (cx, cy).
// Draws one of the TWiLight Menu++ corner icons, centered, scaled by an
// integer factor (nearest neighbor). Index 0 is transparent.
static void home_icon_bitmap(volatile uint8_t *frame, int cx, int cy, unsigned n, int scale) {
  const int w = home_icons[n].w, h = home_icons[n].h;
  const int x0 = cx - w * scale / 2, y0 = cy - h * scale / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = &home_icons[n].data[r * w];
    for (int c = 0; c < w; ) {
      if (!row[c]) { c++; continue; }
      int e = c;
      while (e < w && row[e] == row[c])
        e++;
      car_rect(frame, x0 + c * scale, y0 + r * scale, x0 + e * scale, y0 + (r + 1) * scale, HOME_ICON_BASE + row[c]);
      c = e;
    }
  }
}

// Theme: the brightness sun of TWiLight Menu++ (1:1), dimmed in the dark theme.
// Theme and time sit in the corners, on the cut off part of the grid.
static void home_icon_theme(volatile uint8_t *frame, int cx, int cy) {
  home_icon_bitmap(frame, cx, cy, menu_theme == THEME_DARK ? HOME_ICON_DIM : HOME_ICON_SUN, 1);
}
// Settings: the DS icon of its settings button (its greyed variant in the dark theme).
static void home_icon_settings(volatile uint8_t *frame, int cx, int cy) {
  home_icon_bitmap(frame, cx, cy, menu_theme == THEME_DARK ? HOME_ICON_SETTINGS_DARK : HOME_ICON_SETTINGS, 1);
}
// Time: the alarm clock.
static void home_icon_time(volatile uint8_t *frame, int cx, int cy) {
  home_icon_bitmap(frame, cx, cy, HOME_ICON_CLOCK, 1);
}

// ---------------------------------------------------------------------------
// Top information bar, like the DS one: user name, page dots, time, date and a
// few filler icons, over a gradient of the accent color with dashed
// separators. Time and date take just the room they need.
// ---------------------------------------------------------------------------
#define BAR_H        16
#define BAR_ICONS_X  185        // Filler icons start here (separator just before)

static void bar_text(volatile uint8_t *frame, const char *t, int x) {
  draw_text_idx8_bus16(t, (uint8_t*)&frame[x], SCREEN_WIDTH, BAR_TX);
}

static void bar_icon_layout(volatile uint8_t *frame, int x) {
  car_rect(frame, x, 2, x + 15, 14, 0);
  car_rect(frame, x + 1, 3, x + 14, 7, BAR_ORANGE);
  car_rect(frame, x + 1, 9, x + 14, 13, BAR_ORANGE);
}

static void bar_icon_m(volatile uint8_t *frame, int x) {
  car_rect(frame, x, 2, x + 13, 14, 0);
  car_rect(frame, x + 2, 4, x + 4, 12, BAR_WHITE);                  // The letter M
  car_rect(frame, x + 9, 4, x + 11, 12, BAR_WHITE);
  car_rect(frame, x + 4, 4, x + 6, 7, BAR_WHITE);
  car_rect(frame, x + 7, 4, x + 9, 7, BAR_WHITE);
  car_rect(frame, x + 5, 7, x + 8, 9, BAR_WHITE);
}

static void bar_icon_battery(volatile uint8_t *frame, int x) {
  car_rect(frame, x, 3, x + 20, 13, 0);
  car_rect(frame, x + 20, 6, x + 22, 10, 0);
  car_rect(frame, x + 2, 5, x + 18, 11, BAR_GREEN);
}

// Which page of its group the current tab is (for the page dots): the home
// and clock tabs (the only ones with the info bar) form the group.
static int bar_pages(int *count) {
  switch (smenu.menu_tab) {
  case MENUTAB_HOME:     *count = 2; return 0;
  case MENUTAB_CLOCK:    *count = 2; return 1;
  default:               *count = 0; return -1;
  }
}

static void render_infobar(volatile uint8_t *frame) {
  static const uint8_t bands[4] = { BAR_C0, BAR_C1, BAR_C2, BAR_C3 };
  for (unsigned i = 0; i < 4; i++)
    dma_memset16(&frame[i * 4 * SCREEN_WIDTH], dup8(bands[i]), SCREEN_WIDTH * (i == 3 ? 3 : 4) / 2);
  dma_memset16(&frame[(BAR_H - 1) * SCREEN_WIDTH], dup8(BAR_LN), SCREEN_WIDTH / 2);

  // Time and date (24h, month/day).
  t_dec_date d;
  timestamp2date(clock_now(), &d);
  char tbuf[16], dbuf[16];
  npf_snprintf(tbuf, sizeof(tbuf), "%02d:%02d", d.hour, d.min);
  npf_snprintf(dbuf, sizeof(dbuf), "%02d/%02d", d.month, d.day);

  // Lay out from the right: icons, separator, date, separator, time, separator.
  const int sep3 = BAR_ICONS_X - 3;
  const int sep2 = sep3 - ((int)font_width(dbuf) + 9);
  const int sep1 = sep2 - ((int)font_width(tbuf) + 9);
  const int seps[3] = { sep1, sep2, sep3 };
  for (unsigned i = 0; i < 3; i++)
    for (int y = 1; y < BAR_H - 2; y += 2)
      car_span(frame, y, seps[i], seps[i] + 1, BAR_LN);
  bar_text(frame, tbuf, sep1 + 5);
  bar_text(frame, dbuf, sep2 + 5);

  // Page dots (small squares) just left of the time.
  int npages, cur = bar_pages(&npages);
  int left = sep1 - 5;
  for (int i = npages - 1; i >= 0; i--) {
    left -= 4;
    car_rect(frame, left, 6, left + 4, 10, i == cur ? BAR_TX : BAR_LN);
    left -= 2;
  }

  // User name, clipped to the room that is left.
  char tmp[USERNAME_MAX + 4];
  unsigned n = font_width_cap(user_name, MAX(0, left - 4 - 5));
  memcpy(tmp, user_name, n);
  tmp[n] = 0;
  bar_text(frame, tmp, 5);

  // Filler icons, as in the DS.
  bar_icon_layout(frame, BAR_ICONS_X);
  bar_icon_m(frame, BAR_ICONS_X + 17);
  bar_icon_battery(frame, BAR_ICONS_X + 32);
}

void render_home(volatile uint8_t *frame) {
  ds_background(frame, BG_HOME);

  for (int b = HB_FLASH; b <= HB_CAROUSEL; b++)
    home_button(frame, b);

  char buf[24];
  int x0, y0, x1, y1;

  // Flash games: games written to the cart memory (fast loading ROMs).
  x0 = home_rects[HB_FLASH].x0; y0 = home_rects[HB_FLASH].y0; x1 = home_rects[HB_FLASH].x1; y1 = home_rects[HB_FLASH].y1;
  home_icon_chip(frame, x0 + 8, y0 + 11);
  home_plate(frame, x0 + 44, y0 + 4, x1 - 4, y1 - 4);
  draw_central_text("Internal Flash", frame, (x0 + 44 + x1 - 4) / 2, y0 + 6);
  #ifdef SUPPORT_NORGAMES
  npf_snprintf(buf, sizeof(buf), "%u games", (unsigned)smenu.fbrowser.maxentries);
  #else
  npf_snprintf(buf, sizeof(buf), "Not available");
  #endif
  draw_central_text(buf, frame, (x0 + 44 + x1 - 4) / 2, y0 + 22);

  // Recent games (two tiles wide): icon and a two line label.
  x0 = home_rects[HB_RECENT].x0; y0 = home_rects[HB_RECENT].y0; x1 = home_rects[HB_RECENT].x1; y1 = home_rects[HB_RECENT].y1;
  home_icon_recent(frame, x0 + 5, y0 + 6);
  home_plate(frame, x0 + 42, y0 + 4, x1 - 4, y1 - 4);
  draw_central_text("Recent", frame, (x0 + 42 + x1 - 4) / 2, y0 + 6);
  draw_central_text("games", frame, (x0 + 42 + x1 - 4) / 2, y0 + 22);

  // Hardware test (two tiles wide): icon on the left, two line label on the right.
  x0 = home_rects[HB_HWTEST].x0; y0 = home_rects[HB_HWTEST].y0; x1 = home_rects[HB_HWTEST].x1; y1 = home_rects[HB_HWTEST].y1;
  home_icon_gear(frame, x0 + 8, y0 + 10, 24);
  home_plate(frame, x0 + 42, y0 + 4, x1 - 4, y1 - 4);
  draw_central_text("HW", frame, (x0 + 42 + x1 - 4) / 2, y0 + 6);
  draw_central_text("test", frame, (x0 + 42 + x1 - 4) / 2, y0 + 22);

  // Carousel (four tiles wide).
  x0 = home_rects[HB_CAROUSEL].x0; y0 = home_rects[HB_CAROUSEL].y0; x1 = home_rects[HB_CAROUSEL].x1; y1 = home_rects[HB_CAROUSEL].y1;
  home_icon_cart_small(frame, x0 + 8, y0 + 3);
  home_plate(frame, x0 + 38, y0 + 3, x1 - 4, y1 - 3);
  draw_central_text("Browser", frame, (x0 + 38 + x1 - 4) / 2, y0 + 3);

  // Bottom row: theme, settings and time.
  static void (*const icons[HB_COUNT - HB_THEME])(volatile uint8_t*, int, int) = {
    home_icon_theme, home_icon_settings, home_icon_time,
  };
  for (int b = HB_THEME; b <= HB_TIME; b++)
    icons[b - HB_THEME](frame, (home_rects[b].x0 + home_rects[b].x1) / 2, 148);

  home_brackets(frame, smenu.home.sel);
}

void render_fw_flash_popup(volatile uint8_t *frame) {
  // Render a box to give a pop-up feeling
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_central_text(msgs[lang_id][MSG_FWUPD_MENU], frame, 120, 30);

  draw_box_outline(frame, 16, 224, 64, 92, FG_COLOR);
  if (spop.p.update.issfw) {
    char tmp[32];
    npf_snprintf(tmp, sizeof(tmp), "SuperFW (ver %lu.%lu)",
                 spop.p.update.superfw_ver >> 16,
                 spop.p.update.superfw_ver & 0xFFFF);
    draw_central_text(tmp, frame, 120, 70);
  } else {
    draw_central_text(msgs[lang_id][MSG_FWUPD_UNK], frame, 120, 70);
  }

  const char *smsg[] = {
    msgs[lang_id][MSG_FWUPD_GO],
    msgs[lang_id][MSG_FWUPD_LOADING],
    msgs[lang_id][MSG_FWUPD_CHECKING],
    msgs[lang_id][MSG_FWUPD_ERASING],
    msgs[lang_id][MSG_FWUPD_PROGRAM],
  };

  draw_central_text(smsg[spop.p.update.curr_state], frame, 120, 120);
}

void render_sav_menu_popup(volatile uint8_t *frame) {
  // Render a box to give a pop-up feeling
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  for (unsigned i = 0; i < 3; i++) {
    draw_button_box(frame, 20, 220, 32 + 28 * i, 32 + 28 * i + 20, spop.selector == i);
    draw_central_text(msgs[lang_id][MSG_SAVOPT_OPT0 + i], frame, 120, 34 + 28 * i);
  }
  draw_button_box(frame, 20, 220, 124, 144, spop.selector == SavQuit);
  draw_central_text(msgs[lang_id][MSG_CANCEL], frame, 120, 126);
}

static void render_gbarom_info(volatile uint8_t *frame, const char *dispname,
                               bool issf, const char *gcode, uint8_t ver, int save_type) {
  char tmp[64];
  draw_central_text(msgs[lang_id][MSG_GBALOAD_MINFO], frame, SCREEN_WIDTH/2, 23);

  const char *romname = file_basename(dispname);
  unsigned twidth = font_width(romname);
  if (twidth > SCREEN_WIDTH - 20)
    draw_text_ovf_rotate(romname, frame, 10, 52,
                         SCREEN_WIDTH - 20, &spop.anim);
  else
    draw_central_text_ovf(romname, frame, SCREEN_WIDTH/2, 52, SCREEN_WIDTH - 20);

  npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_GAME], gcode, ver);
  draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 82, SCREEN_WIDTH - 20);

  if (save_type < 0)
    draw_central_text_ovf(msgs[lang_id][MSG_LOADINFO_UNKW], frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  else if (issf)
    draw_central_text_ovf("SuperFW firmware", frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  else {
    const char *stype[] = {
      msgs[lang_id][MSG_SAVETYPE_NONE],       // SaveTypeNone
      msgs[lang_id][MSG_SAVETYPE_SRAM],       // SaveTypeSRAM
      msgs[lang_id][MSG_SAVETYPE_EEPROM],     // SaveTypeEEPROM4K
      msgs[lang_id][MSG_SAVETYPE_EEPROM],     // SaveTypeEEPROM64K
      msgs[lang_id][MSG_SAVETYPE_FLASH],      // SaveTypeFlash512K
      msgs[lang_id][MSG_SAVETYPE_FLASH],      // SaveTypeFlash1024K
    };
    const char *ssize[] = {
      "0KB",       // SaveTypeNone
      "32KB",      // SaveTypeSRAM
      "0.5KB",     // SaveTypeEEPROM4K
      "8KB",       // SaveTypeEEPROM64K
      "64KB",      // SaveTypeFlash512K
      "128KB",     // SaveTypeFlash1024K
    };

    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_LOADINFO_SAVE], stype[save_type], ssize[save_type]);
    draw_central_text_ovf(tmp, frame, SCREEN_WIDTH/2, 102, SCREEN_WIDTH - 20);
  }

  draw_box_full(frame, 20, 220, 132, 152, FG_COLOR, HI_COLOR);
}

static const char *render_gbarom_patching(volatile uint8_t *frame, const t_load_gba_info *info, int selector) {
  draw_central_text(msgs[lang_id][MSG_GBALOAD_MPATCH], frame, SCREEN_WIDTH/2, 23);
  draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 12, 44, 224);
  draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + info->patch_type], frame, 162, 44);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVET], frame, 12, 62, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_ST0 + (info->use_dsaving ? 0 : 1)], frame, 170, 62);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 12, 80, 224);
  draw_central_text(msgs[lang_id][info->ingame_menu_enabled ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 80);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 12, 98, 224);
  draw_central_text(msgs[lang_id][info->rtc_patch_enabled ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 98);

  draw_text_ovf(msgs[lang_id][MSG_LOADER_PTCH], frame, 12, 116, 224);
  draw_box_outline(frame, 170 - 20, 170 + 20, 115, 133, FG_COLOR);
  draw_central_text("▸", frame, 170, 116);

  return (selector == GBALoadPatch) ? msgs[lang_id][MSG_PATCH_TYPE_I0 + info->patch_type] :
         (selector == GBASavePatch) ? msgs[lang_id][MSG_LOADER_ST_I0 + (info->use_dsaving ? 0 : 1)] :
         (selector == GBAInGameMen) ? msgs[lang_id][MSG_INGAME_I] :
         (selector == GBARTCPatch)  ? msgs[lang_id][MSG_PATCHRTC_I] :
         (selector == GBAPatchGen)  ? msgs[lang_id][MSG_PATCHE_I] : NULL;
}

static const char *render_gbarom_loading(volatile uint8_t *frame, const t_load_gba_lcfg *data, bool rtc_patching, int selector) {
  char tmp[64];
  draw_central_text(msgs[lang_id][MSG_GBALOAD_OPTS], frame, SCREEN_WIDTH/2, 23);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 12, 44, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_LOADP0 + data->sram_load_type], frame, 170, 44);
  draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 12, 62, 224);
  draw_central_text(msgs[lang_id][MSG_LOADER_SAVEP0 + data->sram_save_type], frame, 170, 62);
  draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 12, 80, 224);
  if (rtc_patching) {
    t_dec_date d;
    timestamp2date(data->rtcval, &d);
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      d.year, d.month, d.day, d.hour, d.min);
    draw_central_text(tmp, frame, 170, 80);
  }
  else
    draw_central_text("-", frame, 170, 80);
  draw_text_ovf(msgs[lang_id][MSG_SETT_LDCHT], frame, 12, 98, 224);
  draw_central_text(msgs[lang_id][data->use_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, 170, 98);

  draw_box_outline(frame, 170 - 20, 170 + 20, 115, 133, FG_COLOR);
  draw_text_ovf(msgs[lang_id][MSG_SETT_REMEMB], frame, 12, 116, 224);
  render_icon(170-8, 116, ICON_DISK);

  return (selector == GBALdSetLoadP) ? msgs[lang_id][MSG_LOADER_LOADP_I0 + data->sram_load_type] :
         (selector == GBALdSetSaveP) ? msgs[lang_id][MSG_LOADER_SAVEP_I0 + data->sram_save_type] :
         (selector == GBALdSetCheats && !enable_cheats) ? msgs[lang_id][MSG_CHEATSDIS_I] :
         (selector == GBALdSetCheats && !data->cheats_found) ? msgs[lang_id][MSG_CHEATSNOA_I] :
         (selector == GBALdRemember) ? msgs[lang_id][MSG_REMEMB_I] : NULL;
}

void render_gba_load_popup(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);
  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  const t_load_gba_info *info = &spop.p.load.i;
  const t_patch *p = get_game_patch(info);
  const char *ht = NULL;
  switch (spop.submenu) {
  case GbaLoadPopInfo:
    render_gbarom_info(frame, info->romfn, is_superfw(&info->romh), info->gcode,
                       info->romh.version, p ? p->save_mode : -1);
    draw_central_text(msgs[lang_id][MSG_LOAD_GBA], frame, 120, 134);
    break;
  case GbaLoadPopLoadS:
    ht = render_gbarom_loading(frame, &spop.p.load.l, info->rtc_patch_enabled, spop.selector);
    break;
  case GbaLoadPopPatch:
    ht = render_gbarom_patching(frame, &spop.p.load.i, spop.selector);
    break;
  };

  // Show some help if necessary
  if (ht) {
    unsigned twidth = font_width(ht);
    if (twidth > SCREEN_WIDTH - 20)
      draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
    else
      draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
  }

  if (spop.submenu != GbaLoadPopInfo) {
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}

void render_filemgr(volatile uint8_t *frame) {
  // Draw the file name and the options available
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  t_centry *e = sdr_state->fileorder[smenu.browser.selector];
  const char *bn = file_basename(e->fname);

  unsigned twidth = font_width(bn);
  if (twidth > SCREEN_WIDTH - 20)
    draw_text_ovf_rotate(bn, frame, 10, 32, SCREEN_WIDTH - 20, &spop.anim);
  else
    draw_central_text_ovf(bn, frame, SCREEN_WIDTH/2, 32, SCREEN_WIDTH - 20);

  for (unsigned i = 0; i < FiMgrCNT; i++)
    draw_button_box(frame, 20, 220, 60 + i*30, 80 + i*30, i == spop.selector);

  draw_central_text(msgs[lang_id][MSG_FMGR_DEL], frame, 120, 62 + 30*FiMgrDelete);
  draw_central_text(msgs[lang_id][(e->attr & AM_HID) ? MSG_FMGR_UNHIDE : MSG_FMGR_HIDE], frame, 120, 62 + 30*FiMgrHide);

  #ifdef SUPPORT_NORGAMES
  draw_central_text(msgs[lang_id][MSG_NOR_WRITE], frame, 120, 62 + 30*FiMgrWriteNOR);
  #endif
}


#ifdef SUPPORT_NORGAMES
void render_gba_norwrite(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  if (spop.submenu == GbaLoadPopInfo) {
    const t_load_gba_info *info = &spop.p.norwr.i;
    const t_patch *p = get_game_patch(info);
    render_gbarom_info(frame, info->romfn, is_superfw(&info->romh),
                       info->gcode, info->romh.version, p ? p->save_mode : -1);
    draw_central_text(msgs[lang_id][MSG_NOR_WRITE], frame, 120, 134);
  } else {
    const char *ht = render_gbarom_patching(frame, &spop.p.norwr.i, spop.selector);
    if (ht) {
      unsigned twidth = font_width(ht);
      if (twidth > SCREEN_WIDTH - 20)
        draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
      else
        draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
    }
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}

void render_gba_norload(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_text_ovf("⯇", frame, 10, 23, 64);
  draw_rightj_text("⯈", frame, SCREEN_WIDTH - 10, 23);

  const t_flash_game_entry *e = spop.p.norld.e;
  if (spop.submenu == GbaLoadPopInfo) {
    int save_type = GET_GATTR_SAVEM(e->gattrs);
    char gcode[5] = {
      e->gamecode & 0xFF, (e->gamecode >> 8) & 0xFF, (e->gamecode >> 16) & 0xFF, e->gamecode >> 24, 0
    };
    render_gbarom_info(frame, e->game_name, false, gcode, e->gamever, save_type);
    draw_central_text(msgs[lang_id][MSG_NOR_LAUNCH], frame, 120, 134);
  } else {
    bool rtc_patching = e->gattrs & GATTR_RTC;
    const char *ht = render_gbarom_loading(frame, &spop.p.norld.l, rtc_patching, spop.selector);
    if (ht) {
      unsigned twidth = font_width(ht);
      if (twidth > SCREEN_WIDTH - 20)
        draw_text_ovf_rotate(ht, frame, 10, 137, SCREEN_WIDTH - 20, &spop.anim);
      else
        draw_central_text_ovf(ht, frame, SCREEN_WIDTH/2, 137, SCREEN_WIDTH - 20);
    }
    const unsigned offy = 43;
    for (unsigned i = 8; i < 232; i += 16) {
      render_icon_trans(i, offy + 0 + spop.selector * 18, 63);
      render_icon_trans(i, offy + 2 + spop.selector * 18, 63);
    }
  }
}
#endif

void render_popupq(volatile uint8_t *frame, unsigned fcnt) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  // Draw question and two buttons
  draw_central_text_wrapped(spop.qpop.message, frame, SCREEN_WIDTH/2, 32, SCREEN_WIDTH - 20);

  if (spop.qpop.option == 0) {
    draw_box_full(frame, 20, 220, 90, 90 + 20, FG_COLOR, HI_COLOR);
    draw_box_outline(frame, 20, 220, 120, 120 + 20, FG_COLOR);
  } else {
    draw_box_full(frame, 20, 220, 120, 120 + 20, FG_COLOR, HI_COLOR);
    draw_box_outline(frame, 20, 220, 90, 90 + 20, FG_COLOR);
  }

  draw_central_text(spop.qpop.default_button, frame, 120, 92);
  draw_central_text(spop.qpop.confirm_button, frame, 120, 122);
}

void render_rtcpop(volatile uint8_t *frame) {
  draw_box_outline(frame, 2, 240-2, 18, 158, FG_COLOR);

  draw_central_text(spop.rtcpop.title ?: msgs[lang_id][MSG_DEF_RTCVAL], frame, SCREEN_WIDTH/2, 32);

  const t_dec_date *v = &spop.rtcpop.val;
  char thour[3] = {'0' + v->hour /10, '0' + v->hour  % 10, 0};
  char tmins[3] = {'0' + v->min  /10, '0' + v->min   % 10, 0};
  char tdays[3] = {'0' + v->day  /10, '0' + v->day   % 10, 0};
  char tmont[3] = {'0' + v->month/10, '0' + v->month % 10, 0};
  char tyear[5] = {'2', '0', '0' + v->year/10, '0' + v->year % 10, 0};

  draw_central_text(tyear, frame,  60, 70);
  draw_central_text("-",   frame,  80, 70);
  draw_central_text(tmont, frame,  94, 70);
  draw_central_text("-",   frame, 106, 70);
  draw_central_text(tdays, frame, 120, 70);
  draw_central_text(thour, frame, 154, 70);
  draw_central_text(":",   frame, 166, 70);
  draw_central_text(tmins, frame, 180, 70);

  const uint8_t cox[] = {
    60, 94, 120, 154, 180
  };
  draw_central_text("⯅", frame, cox[spop.rtcpop.selector], 54);
  draw_central_text("⯆", frame, cox[spop.rtcpop.selector], 84);
}

void render_settings(volatile uint8_t *frame) {
  char tmp[128];
  unsigned baseopt = smenu.set.selector <= 2  ? 0 :
                     smenu.set.selector >= SettMAX - 3 ? SettMAX - 5 :
                     smenu.set.selector - 2;

  if (smenu.set.selector > 2)
    draw_central_text("⯅", frame, 120, 15);
  if (smenu.set.selector < SettSave - 2)
    draw_central_text("⯆", frame, 120, 125);

  const unsigned maxrows = 5;
  unsigned optnum = 0, optcnt = 0;
  const unsigned colx = 170;           // Center point for the selection boxes
  const unsigned offy = 29;
  const unsigned rowh = 20;

  if (optnum++ >= baseopt && optcnt < maxrows)
    draw_central_text(msgs[lang_id][MSG_SET_TITL1], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (optnum++ >= baseopt && optcnt < maxrows) {
    npf_snprintf(tmp, sizeof(tmp), "< %s >", hotkey_list[hotkey_combo].cname);
    draw_text_ovf(msgs[lang_id][MSG_SETT_HOTK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_BOOT], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_BOOT_TYPE0 + boot_bios_splash], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTSD], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_slowld ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, offy + rowh*optcnt++);
  }

  #ifdef SUPPORT_NORGAMES
  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_VERNOR], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_verify_nor ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }
  #endif

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_FASTEW], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][use_fastew ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVET], frame, 8, offy + rowh*optcnt, 224);

    if (save_path_default == SaveRomName)
      draw_central_text(msgs[lang_id][MSG_NEXTTO_ROM], frame, colx, offy + rowh*optcnt++);
    else {
      npf_snprintf(tmp, sizeof(tmp), "< %s >", save_paths[save_path_default]);
      draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
    }
  }

  #ifdef SUPPORT_NORGAMES
  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVETX], frame, 8, offy + rowh*optcnt, 224);
    npf_snprintf(tmp, sizeof(tmp), "< %s >", save_paths[save_path_nor_default]);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }
  #endif

  if (optnum++ >= baseopt && optcnt < maxrows) {
    npf_snprintf(tmp, sizeof(tmp), "< %u >", backup_sram_default);
    draw_text_ovf(msgs[lang_id][MSG_SETT_SAVEBK], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++ );
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_STATET], frame, 8, offy + rowh*optcnt, 224);
    npf_snprintf(tmp, sizeof(tmp), "< %s >", savestates_paths_display[state_path_default]);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_SETT_CHTEN], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][enable_cheats ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows)
    draw_central_text(msgs[lang_id][MSG_SET_TITL2], frame, SCREEN_WIDTH/2, offy + rowh*optcnt++);

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_DEFS_PATCH], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_PATCH_TYPE0 + patcher_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_MENU], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + ingamemenu_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_RTCE], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_KNOB_DISABLED + rtcpatch_default], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    t_dec_date d;
    timestamp2date(rtcvalue_default, &d);
    npf_snprintf(tmp, sizeof(tmp), "20%02d/%02d/%02d %02d:%02d",
      d.year, d.month, d.day, d.hour, d.min);
    draw_text_ovf(msgs[lang_id][MSG_DEF_RTCVAL], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    unsigned spdmsg = rtcspeed_default ? (MSG_UIS_SPD0 + rtcspeed_default - 1) :
                                          MSG_STILLRTC;
    npf_snprintf(tmp, sizeof(tmp), "< %s >", msgs[lang_id][spdmsg]);
    draw_text_ovf(msgs[lang_id][MSG_DEF_SPEED], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(tmp, frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_LOADP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][MSG_DEF_LOADP0 + (autoload_default ^ 1)], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_SAVEP], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_default ? MSG_DEF_SAVEP0 : MSG_DEF_SAVEP1], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_text_ovf(msgs[lang_id][MSG_LOADER_PREFDS], frame, 8, offy + rowh*optcnt, 224);
    draw_central_text(msgs[lang_id][autosave_prefer_ds ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, offy + rowh*optcnt++);
  }

  if (optnum++ >= baseopt && optcnt < maxrows) {
    draw_button_box(frame, 20, 220, 112, 132, smenu.set.selector == SettSave);
    draw_central_text(msgs[lang_id][MSG_UIS_SAVE], frame, 132, 114);
  }

  // Render bar below for help messge
  dma_memset16(&frame[240*148], dup8(FG_COLOR), 240*20/2);

  if (smenu.set.selector == SettSaveLoc) {
    if (save_path_default == SaveRomName)
      draw_text_ovf_rotate(msgs[lang_id][MSG_SAVE_TYPE_NR], frame, 4, SCREEN_HEIGHT - 10, 232, &smenu.anim_state);
    else {
      npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_SAVE_TYPE_PT], save_paths[save_path_default]);
      draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 10, 232, &smenu.anim_state);
    }
  }
  else if (smenu.set.selector == SettStateLoc) {
    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_STATE_TYPE_PT], savestates_paths[state_path_default]);
    draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 10, 232, &smenu.anim_state);
  }
  #ifdef SUPPORT_NORGAMES
  else if (smenu.set.selector == SettSaveLocNOR) {
    npf_snprintf(tmp, sizeof(tmp), msgs[lang_id][MSG_SAVE_TYPE_PTX], save_paths[save_path_nor_default]);
    draw_text_ovf_rotate(tmp, frame, 4, SCREEN_HEIGHT - 10, 232, &smenu.anim_state);
  }
  #endif
  else {
    unsigned help_msg = smenu.set.selector == SettBootType ? MSG_BOOT_TYPE_I0 + boot_bios_splash :
                        smenu.set.selector == SettSaveBkp  ? MSG_BACKUP_I :
                        smenu.set.selector == SettFastSD   ? MSG_FASTSD_I :
                        smenu.set.selector == SettFastEWRAM? MSG_FASTEW_I :
                        smenu.set.selector == DefsPatchEng ? MSG_PATCH_TYPE_I0 + patcher_default :
                        smenu.set.selector == DefsLoadPol  ? MSG_DEF_LOADP_I0 + (autoload_default ^ 1) :
                        smenu.set.selector == DefsSavePol  ? MSG_DEF_SAVEP_I0 + (autosave_default ^ 1) :
                        smenu.set.selector == DefsPrefDS   ? MSG_LOADER_PREFDSI :
                        #ifdef SUPPORT_NORGAMES
                        smenu.set.selector == SettVerifyNOR ? MSG_VERNOR_I :
                        #endif
                        MSG_EMPTY;
    draw_text_ovf_rotate(msgs[lang_id][help_msg], frame, 4, SCREEN_HEIGHT - 10, 232, &smenu.anim_state);
  }

  if (smenu.set.selector != SettSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, offy + (smenu.set.selector - baseopt) * 20, 63);
}

void render_ui_settings(volatile uint8_t *frame) {
  const unsigned colx = 170, y0 = 18, step = 16;
  char tmpbuf[64];
  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %u >", menu_theme + 1U);
  draw_text_ovf(msgs[lang_id][MSG_UIS_THEME], frame, 8, y0, 224);
  draw_central_text(tmpbuf, frame, colx, y0);

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", msgs[lang_id][MSG_LANG_NAME]);
  draw_text_ovf(msgs[lang_id][MSG_UIS_LANG], frame, 8, y0 + step, 224);
  draw_central_text(tmpbuf, frame, colx, y0 + step);

  draw_text_ovf(msgs[lang_id][MSG_UIS_RECNT], frame, 8, y0 + step * 2, 224);
  draw_central_text(msgs[lang_id][recent_menu ? MSG_KNOB_ENABLED : MSG_KNOB_DISABLED], frame, colx, y0 + step * 2);

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", msgs[lang_id][MSG_UIS_SPD0 + anim_speed]);
  draw_text_ovf(msgs[lang_id][MSG_UIS_ANSPD], frame, 8, y0 + step * 3, 224);
  draw_central_text(tmpbuf, frame, colx, y0 + step * 3);

  draw_text_ovf(msgs[lang_id][MSG_UIS_BHID], frame, 8, y0 + step * 4, 224);
  draw_central_text(msgs[lang_id][hide_hidden ? MSG_KNOB_DISABLED : MSG_KNOB_ENABLED], frame, colx, y0 + step * 4);

  npf_snprintf(tmpbuf, sizeof(tmpbuf), "< %s >", accents[accent_color % ACCENT_COUNT].name);
  draw_text_ovf("Accent color", frame, 8, y0 + step * 5, 224);
  draw_central_text(tmpbuf, frame, colx, y0 + step * 5);

  draw_text_ovf("User name", frame, 8, y0 + step * 6, 224);
  if (smenu.uiset.editing) {
    // Show the whole (padded) name with an underline on the edited character.
    const int sx = (int)colx - (int)font_width(user_name) / 2;
    draw_text_idx8_bus16(user_name, (uint8_t*)&frame[(y0 + step * 6) * SCREEN_WIDTH + sx], SCREEN_WIDTH, FT_COLOR);
    char pre[USERNAME_MAX + 1], one[2] = { user_name[smenu.uiset.cursor], 0 };
    memcpy(pre, user_name, smenu.uiset.cursor);
    pre[smenu.uiset.cursor] = 0;
    const int cx = sx + (int)font_width(pre);
    car_rect(frame, cx, y0 + step * 6 + 14, cx + (int)font_width(one), y0 + step * 6 + 16, ACC_COLOR);
  } else {
    draw_central_text(user_name[0] ? user_name : "-", frame, colx, y0 + step * 6);
  }

  if (smenu.uiset.selector != UiSetSave)
    for (unsigned i = 0; i < 240; i += 16)
      render_icon_trans(i, y0 + smenu.uiset.selector * step, 63);

  draw_button_box(frame, 20, 220, 136, 156, smenu.uiset.selector == UiSetSave);
  draw_central_text(msgs[lang_id][MSG_UIS_SAVE], frame, 120, 138);
}

void render_info(volatile uint8_t *frame) {
  uint32_t vmaj = VERSION_WORD >> 16;
  uint32_t vmin = VERSION_WORD & 0xFFFF;
  uint32_t gitver = VERSION_SLUG_WORD;
  char tmp[64], tmp2[32];

  init_logo_palette(&MEM_PALETTE[1]);
  render_logo((uint16_t*)frame, SCREEN_WIDTH/2, 40, 4);

  switch (smenu.info.selector) {
  case 0:
    // Title, drawn twice one pixel apart to look bolder.
    draw_central_text("AGoodShell++", frame, 120, 64);
    draw_central_text("AGoodShell++", frame, 121, 64);
    draw_central_text("based on SuperFW by davidgf", frame, 120, 82);
    npf_snprintf(tmp, sizeof(tmp), "Version %lu.%lu (%08lx)", vmaj, vmin, gitver);
    draw_central_text(tmp, frame, 120, 100);
    draw_central_text(FW_FLAVOUR " variant", frame, 120, 118);
    break;
  case 1:
    draw_central_text("Flash info", frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "Dev ID: %08lx", flashinfo.deviceid);
    draw_central_text(tmp, frame, 120, 95);
    if (flashinfo.size && flashinfo.blksize && flashinfo.blkcount) {
      human_size_kb(tmp2, sizeof(tmp2), flashinfo.size >> 10);
      npf_snprintf(tmp, sizeof(tmp), "%s [%lu * %lu]", tmp2, flashinfo.blksize, flashinfo.blkcount);
      if (flashinfo.regioncnt != 1)
        strcat(tmp, " !");
      draw_central_text(tmp, frame, 120, 115);
    } else {
      npf_snprintf(tmp, sizeof(tmp), "No CFI! (hardwired %dKiB)", FW_MAX_SIZE_KB);
      draw_central_text(tmp, frame, 120, 115);
    }
    break;
  case 2:
    draw_central_text(msgs[lang_id][MSG_DBPINFO], frame, 120, 70);
    npf_snprintf(tmp, sizeof(tmp), "%s - %s", pdbinfo.version, pdbinfo.date);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Game count: %lu", pdbinfo.patch_count);
    draw_central_text(tmp, frame, 120, 110);
    break;
  case 3:
    npf_snprintf(tmp, sizeof(tmp), "SD Card ID: %02x | %04x", sd_info.manufacturer, sd_info.oemid);
    draw_central_text(tmp, frame, 120, 70);
    human_size_kb(tmp2, sizeof(tmp2), sd_info.block_cnt / 2);
    npf_snprintf(tmp, sizeof(tmp), "Type: %s  Size: %s", sd_info.sdhc ? "SDHC" : "SDSC", tmp2);
    draw_central_text(tmp, frame, 120, 90);
    npf_snprintf(tmp, sizeof(tmp), "Info: %c%c%c%c%c (%d/%d)",
      sd_info.prodname[0] ?: '#', sd_info.prodname[1] ?: '#',
      sd_info.prodname[2] ?: '#', sd_info.prodname[3] ?: '#',
      sd_info.prodname[4] ?: '#', 2000 + sd_info.year, sd_info.month);
    draw_central_text(tmp, frame, 120, 110);
    break;
  }

  // Flashing info
  dma_memset16(&frame[138*SCREEN_WIDTH], dup8(FG_COLOR), SCREEN_WIDTH*22/2);
  draw_text_ovf_rotate(enable_flashing ? msgs[lang_id][MSG_FWUP_ENABLED] : msgs[lang_id][MSG_FWUP_HOTKEY], frame,
                       4, 141, SCREEN_WIDTH - 8, &smenu.anim_state);
}

void render_tools(volatile uint8_t *frame) {
  for (unsigned i = 0; i < ToolsMAX; i++)
    draw_text_ovf(msgs[lang_id][MSG_TOOLS0_SDRAM + i], frame, 22, 26 + 22 * i, 144);

  smenu.anim_state = (smenu.anim_state + 1) & 255;
  draw_central_text("▸", frame, 11 + (smenu.anim_state >> 6), 26 + 22 * smenu.tools.selector);

  for (unsigned i = 0; i < 240; i += 16)
    render_icon_trans(i, 26 + smenu.tools.selector * 22, 63);
}

void reload_theme(unsigned thnum) {
  // Palette 0..15 contains the main menu template colors
  MEM_PALETTE[FG_COLOR] = themes[thnum].fg_color;
  MEM_PALETTE[BG_COLOR] = themes[thnum].bg_color;
  MEM_PALETTE[FT_COLOR] = themes[thnum].ft_color;
  MEM_PALETTE[HI_COLOR] = themes[thnum].hi_color;

  // Accent color: selection brackets and the info bar gradient.
  const uint32_t rgb = accents[accent_color % ACCENT_COUNT].rgb;
  const uint16_t acc = RGB2GBA(rgb);
  const bool light = (((rgb >> 16) & 0xFF) * 3 + ((rgb >> 8) & 0xFF) * 6 + (rgb & 0xFF)) / 10 > 150;
  MEM_PALETTE[ACC_COLOR] = acc;
  const bool dark = thnum == THEME_DARK;
  const int dk = dark ? 150 : 0;       // The dark theme has a darker bar, black grid and darker buttons
  MEM_PALETTE[BAR_C0] = mix15(mix15(acc, 0x7FFF, 100), 0, dk);
  MEM_PALETTE[BAR_C1] = mix15(mix15(acc, 0x7FFF, 45), 0, dk);
  MEM_PALETTE[BAR_C2] = mix15(acc, 0, dk);
  MEM_PALETTE[BAR_C3] = mix15(mix15(acc, 0, 40), 0, dk);
  MEM_PALETTE[BAR_LN] = mix15(mix15(acc, 0, 130), 0, dk);
  MEM_PALETTE[BAR_TX] = light ? RGB2GBA(0x101018) : 0x7FFF;
  MEM_PALETTE[BAR_GREEN] = RGB2GBA(0x30d040);
  MEM_PALETTE[BAR_ORANGE] = RGB2GBA(0xf08020);
  MEM_PALETTE[BAR_WHITE] = 0x7FFF;
  if (dark) {
    // Dark theme: dark calendar and clock face, light text, black grid lines.
    MEM_PALETTE[CAL_BG]    = RGB2GBA(0x2a2e36);
    MEM_PALETTE[CAL_TXT]   = RGB2GBA(0xe0e2e8);
    MEM_PALETTE[CAL_RED]   = RGB2GBA(0xff6868);
    MEM_PALETTE[CAL_BLUE]  = RGB2GBA(0x78a0ff);
    MEM_PALETTE[CAL_HRED]  = RGB2GBA(0x982020);
    MEM_PALETTE[CAL_HBLUE] = RGB2GBA(0x203c98);
    MEM_PALETTE[CAL_HGRAY] = RGB2GBA(0x404048);
    MEM_PALETTE[CAL_PINK]  = RGB2GBA(0x3c2830);
    MEM_PALETTE[CAL_LBLUE] = RGB2GBA(0x262e48);
    MEM_PALETTE[CAL_LINE]  = 0;
    MEM_PALETTE[CLK_HAND]  = RGB2GBA(0xa8a8a8);
    MEM_PALETTE[CLK_PIN]   = RGB2GBA(0xd8d8d8);
    MEM_PALETTE[CLK_TICK]  = RGB2GBA(0x70747c);
    MEM_PALETTE[CLK_FGRID] = RGB2GBA(0x1a1c22);
  } else {
    MEM_PALETTE[CAL_BG]    = 0x7FFF;
    MEM_PALETTE[CAL_TXT]   = RGB2GBA(0x303038);
    MEM_PALETTE[CAL_RED]   = RGB2GBA(0xd01c1c);
    MEM_PALETTE[CAL_BLUE]  = RGB2GBA(0x1c3cd0);
    MEM_PALETTE[CAL_HRED]  = RGB2GBA(0xd83030);
    MEM_PALETTE[CAL_HBLUE] = RGB2GBA(0x3058d8);
    MEM_PALETTE[CAL_HGRAY] = RGB2GBA(0x606068);
    MEM_PALETTE[CAL_PINK]  = RGB2GBA(0xf8c8dc);
    MEM_PALETTE[CAL_LBLUE] = RGB2GBA(0xc8d8f8);
    MEM_PALETTE[CAL_LINE]  = RGB2GBA(0x9098a0);
    MEM_PALETTE[CLK_HAND]  = RGB2GBA(0x797979);   // Needle and pin colors from TWiLight Menu++
    MEM_PALETTE[CLK_PIN]   = RGB2GBA(0x494949);
    MEM_PALETTE[CLK_TICK]  = RGB2GBA(0xb2b2b2);   // Numbers and grid of the TWiLight Menu++ clock face
    MEM_PALETTE[CLK_FGRID] = RGB2GBA(0xd3d3d3);
  }
  {
    // DSi menu colors (TWiLight Menu++ DSi theme): white plates and bubble,
    // steel blue slot; darkened in the dark theme.
    static const uint32_t plate[8] = { 0xf8f8f8, 0xf0f0f0, 0xe8e8e8, 0xe0e0e0, 0xd8d8d8, 0xd0d0d0, 0xa0a0a0, 0xb8b8b8 };
    static const uint32_t blue[8] = { 0x98b0c0, 0x88a4b8, 0x7898b0, 0x6890a8, 0x608098, 0x587890, 0x3c5870, 0x306c38 };
    for (unsigned i = 0; i < 8; i++) {
      MEM_PALETTE[PL_BASE + i] = mix15(RGB2GBA(plate[i]), 0, dark ? 170 : 0);
      MEM_PALETTE[BL_BASE + i] = mix15(RGB2GBA(blue[i]), 0, dark ? 110 : 0);
    }
  }
  // Scanline rows are a bit closer to the text color than the others.
  MEM_PALETTE[SL_BG] = mix15(themes[thnum].bg_color, themes[thnum].ft_color, 14);
  const uint16_t grid = dark ? 0 : themes[thnum].hi_color;
  MEM_PALETTE[GRID_COLOR] = grid;
  MEM_PALETTE[SL_GRID] = mix15(grid, themes[thnum].ft_color, 14);
  for (unsigned i = 0; i < HOME_ICON_COLORS; i++)
    MEM_PALETTE[HOME_ICON_BASE + 1 + i] = home_icon_pal[i];
  MEM_PALETTE[BTN_EDGE] = dark ? themes[thnum].fg_color : themes[thnum].ft_color;
  // Button gradient: lighter at the top (darker in the dark theme), many shades.
  {
    const uint16_t hi = themes[thnum].hi_color;
    const uint16_t top = dark ? mix15(hi, 0, 120) : mix15(hi, 0x7FFF, 150);
    const uint16_t bot = dark ? mix15(hi, 0, 235) : mix15(hi, themes[thnum].ft_color, 24);
    for (int i = 0; i < GRAD_N; i++)
      MEM_PALETTE[GRAD_BASE + i] = mix15(top, bot, i * 256 / (GRAD_N - 1));
  }
  // In-game menu palette
  MEM_PALETTE[IGM_PAL_FG] = themes[thnum].fg_color;
  MEM_PALETTE[IGM_PAL_BG] = themes[thnum].bg_color;
  MEM_PALETTE[IGM_PAL_HI] = themes[thnum].ft_color;
  MEM_PALETTE[IGM_PAL_SH] = themes[thnum].sh_color;
  MEM_PALETTE[IGM_PAL_BL] = themes[thnum].hi_blend;

  // Palette entries for icons and other objects
  MEM_PALETTE[256 + SEL_COLOR] = themes[thnum].hi_blend;
}

static const struct {
  const t_mrender_fn render;
  const int max_submenu;
} popup_windows[] = {
  { render_gba_load_popup, GbaLoadCNT },
  { render_sav_menu_popup, 1 },
  { render_fw_flash_popup, 1 },
  { render_filemgr,        1 },
  #ifdef SUPPORT_NORGAMES
  { render_gba_norwrite,   GbaNorWrCNT },
  { render_gba_norload,    GbaNorLoadCNT },
  #endif
};

// Renders the menu. Arg0 represents the frame count difference with the
// previous rendered frame (for animations and similar stuff).
// The recent ROMs tab is only available when enabled and not empty.
static bool recent_available(void) {
  return recent_menu && smenu.recent.maxentries;
}

// Clock tick: a very short noise burst on the PSG every second, only while
// the clock tab is open. The sound hardware is switched off otherwise.
static void clock_tick_update(void) {
  static bool snd_on;
  static uint32_t last_ts;
  if (smenu.menu_tab == MENUTAB_CLOCK) {
    const uint32_t ts = clock_now();
    if (!snd_on) {
      SNDCNT_X = 0x80;               // Master enable
      SNDCNT_L = 0x8855;             // Noise channel to both sides, volume 5/7
      SNDCNT_H = 2;                  // PSG at 100%
      snd_on = true;
      last_ts = ts;
    }
    if (ts != last_ts) {
      last_ts = ts;
      SND4_L = (10 << 12) | (1 << 8) | 58;                      // Volume 10, fast decay, ~23ms length
      SND4_H = 0x8000 | 0x4000 | ((ts & 1) ? (3 << 4) : (2 << 4)) | 1;   // Tick and tock pitches
    }
  } else if (snd_on) {
    SNDCNT_X = 0;
    snd_on = false;
  }
}

void menu_render(unsigned fcnt) {
  objnum = 0;
  render_fcnt = fcnt;
  volatile uint8_t *frame = &MEM_VRAM_U8[0xA000*framen];

  // Like in TWiLight Menu++ only the home and clock tabs have the information
  // bar; the others use the whole screen. Their content is laid out for a 16 row
  // bar, so it is drawn shifted up (16 rows for the lists and the carousel, which
  // use all the room, 8 for the others, which end up centered).
  const bool bar = smenu.menu_tab == MENUTAB_HOME || smenu.menu_tab == MENUTAB_CLOCK;
  int shift = 0;
  if (!bar)
    shift = (smenu.menu_tab == MENUTAB_RECENT || smenu.menu_tab == MENUTAB_ROMBROWSE
             #ifdef SUPPORT_NORGAMES
             || smenu.menu_tab == MENUTAB_NORBROWSE
             #endif
            ) ? 16 : 8;
  if (bar) {
    render_infobar(frame);
    dma_memset16(&frame[16*SCREEN_WIDTH], dup8(BG_COLOR), SCREEN_WIDTH*(SCREEN_HEIGHT-16) / 2);
  } else {
    dma_memset16(&frame[0], dup8(BG_COLOR), SCREEN_WIDTH*SCREEN_HEIGHT / 2);
  }

  if (spop.qpop.message)
    render_popupq(frame, fcnt);
  else if (spop.rtcpop.callback)
    render_rtcpop(frame);
  else {
    if (spop.pop_num) {
      popup_windows[spop.pop_num - 1].render(frame);
      spop.anim += fcnt * animspd_lut[anim_speed];
    } else {
      static const t_mrender_fn renderfns[] = {
        render_home,
        render_recent,
        render_carousel,
        #ifdef SUPPORT_NORGAMES
        render_flashbrowser,
        #endif
        render_clock,
        render_settings,
        render_ui_settings,
        render_tools,
        render_info,
      };
      ui_yshift = shift;
      renderfns[smenu.menu_tab](frame - shift * SCREEN_WIDTH);
      ui_yshift = 0;
      smenu.anim_state += fcnt * animspd_lut[anim_speed];
    }
  }

  clock_tick_update();

  // Render popup window. Use windowing to ensure the pop up is not covered by OBJs.
  if (spop.alert_msg) {
    draw_box_full(frame, 15, 227, SCREEN_HEIGHT / 2 - 20, SCREEN_HEIGHT / 2 + 20, FG_COLOR, HI_COLOR);
    draw_central_text(spop.alert_msg, frame, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 8);
    REG_WIN0H = 226 | (14 << 8);
    REG_WIN0V = (SCREEN_HEIGHT / 2 + 20) | ((SCREEN_HEIGHT / 2 - 20) << 8);
  } else {
    REG_WIN0H = 0;
    REG_WIN0V = 0;
  }
}

void menu_flip() {
  for (unsigned i = 0; i < objnum; i++) {
    MEM_OAM[i*4+0] = fobjs[i].y | 0x2000;  // Use 256 entries palette
    MEM_OAM[i*4+1] = fobjs[i].x | 0x4000;  // Size 16x16
    MEM_OAM[i*4+2] = fobjs[i].tn + 512;    // OBJ numbers start at 512 for Mode 4
  }
  dma_memset16(&MEM_OAM[objnum*4], 0, 256 - objnum*2);  // Clear unused objects
  REG_DISPCNT = (REG_DISPCNT & ~0x10) | (framen << 4);
  framen ^= 1;
}

void menu_init(int sram_testres) {
  // Reset to ROM browser and SD card root.
  memset(&smenu, 0, sizeof(smenu));
  memset(&spop, 0, sizeof(spop));

  // Start the software clock from the last time the user set.
  clock_base_ts = clock_ts_default;
  clock_base_frame = frame_count;
  car_pos = 0;

  // Reset the file browser as well.
  strcpy(smenu.browser.cpath, "/");
  browser_reload();
  flashbrowser_reload();

  // Load recent ROMs (we could disable this for speed)
  recent_reload();

  bg_init();
  reload_theme(menu_theme);

  smenu.menu_tab = MENUTAB_HOME;
  smenu.home.sel = HB_CAROUSEL;
  smenu.home.row2 = HB_RECENT;

  // Load icons into VRAM
  dma_memcpy16(MEM_VRAM_OBJS, icons_img, sizeof(icons_img) / 2);
  dma_memcpy16(&MEM_PALETTE[256], icons_pal, sizeof(icons_pal) / 2);
  // Generate some icons (selector)
  dma_memset16(&MEM_VRAM_OBJS[63 * 256], dup8(SEL_COLOR), 256 / 2);

  // Further setup initial video regs. BG2 is setup in the bootloader already!
  REG_WININ  = 0x0004;     // Only BG2 is enabled in Win0
  REG_WINOUT = 0x0014;     // BG2 and OBJ enabled outside of Win0
  REG_WIN0H = 0;
  REG_WIN0V = 0;
  REG_DISPCNT |= 0x2000;   // Enable window 0

  // Setup alpha blending for the selector knob
  REG_BLDCNT = 0x1F40;
  REG_BLDALPHA = 0x0808;  // 50% alpha

  // If there's a test result to report, create a popup
  if (sram_testres >= 0)
    spop.alert_msg = sram_testres ? msgs[lang_id][MSG_SRAMTST_FAIL] :
                                    msgs[lang_id][MSG_SRAMTST_OK];
}

int movedir_up() {
  char *p = smenu.browser.cpath;
  p = &p[strlen(p)-1];

  if (p != smenu.browser.cpath) {
    do {
      p--;
      if (*p == '/') {
        p[1] = 0;   // Shorten the path here
        return 1;
      }
    } while (p != smenu.browser.cpath);
  }
  return 0;
}

void start_flash_update(const char *fn, unsigned fwsize, bool validate_superfw) {
  // We read the file into SDRAM, apply the update from there.
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
  else {
    // Loading file...
    spop.p.update.curr_state = FlashingLoading;
    menu_render(1); menu_flip();
    for (unsigned i = 0; i < fwsize; i += 4*1024) {
      UINT rdbytes;
      unsigned tord = fwsize >= i + 4*1024 ? 4*1024 : fwsize - i;
      uint32_t tmp[1024];
      if (FR_OK != f_read(&fd, tmp, tord, &rdbytes) || rdbytes != tord) {
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRRD];
        return;
      }
      // Copy (ensure aligned copy!)
      dma_memcpy32(&sdr_state->scratch[i], tmp, 1024);
    }
    spop.p.update.curr_state = FlashingChecking;
    menu_render(1); menu_flip();

    // Now proceed to validate the superfw if necessary.
    if (validate_superfw && !validate_superfw_variant(sdr_state->scratch))
      spop.alert_msg = msgs[lang_id][MSG_FWUP_BADFL];
    else if (validate_superfw && !validate_superfw_checksum(sdr_state->scratch, fwsize))
      spop.alert_msg = msgs[lang_id][MSG_FWUPD_BADCHK];
    else {
      // Can start the flashing!
      spop.p.update.curr_state = FlashingErasing;
      menu_render(1); menu_flip();

      bool erased_ok;
      #ifdef SUPPORT_NORGAMES
      if (flashinfo.blksize)
        erased_ok = flash_erase_sectors(ROM_FLASHFIRMW_ADDR, flashinfo.blksize,
                                        (fwsize + flashinfo.blksize - 1) / flashinfo.blksize);
      else
      #endif
        erased_ok = flash_erase_chip();

      if (!erased_ok)
        spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRCL];
      else {
        spop.p.update.curr_state = FlashingWriting;
        menu_render(1); menu_flip();

        bool programmed_ok;
        #ifdef SUPPORT_NORGAMES
        if (flashinfo.size && flashinfo.blksize && flashinfo.blkcount && flashinfo.blkwrite)
          programmed_ok = flash_program_buffered(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize, flashinfo.blkwrite);
        else
        #endif
          programmed_ok = flash_program(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize);

        if (!programmed_ok)
          spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRPG];
        else {
          if (!flash_verify(ROM_FLASHFIRMW_ADDR, sdr_state->scratch, fwsize))
            spop.alert_msg = msgs[lang_id][MSG_FWUP_ERRVR];
          else {
            // Done! Show a pop up, also go up with pop ups too.
            spop.alert_msg = msgs[lang_id][MSG_FWUPD_DONE];
          }
        }
      }
    }
    spop.pop_num = 0;
  }
}

static void keypress_popup_loadgba(unsigned newkeys) {
  const unsigned maxm[] = {
    GBAInfoCNT,
    GBALdSetCNT,
    GBAPatchCNT,
  };
  unsigned maxsel = maxm[(int)spop.submenu];

  const int psel = spop.selector;
  if (newkeys & KEY_BUTTUP)
    spop.selector += maxsel - 1;
  if (newkeys & KEY_BUTTDOWN)
    spop.selector++;

  // Limit selector to its max value
  spop.selector %= maxsel;

  if (newkeys & KEY_BUTTLEFT) {
    if (spop.submenu == GbaLoadPopLoadS) {
      if (spop.selector == GBALdSetCheats)
        spop.p.load.l.use_cheats = !spop.p.load.l.use_cheats;
      if (spop.p.load.i.use_dsaving) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + SaveLoadDSCNT - 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + SaveLoadCNT - 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.load.l.sram_save_type = (spop.p.load.l.sram_save_type + SaveCNT - 1) % SaveCNT;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch) {
      if (spop.selector == GBALoadPatch)
        spop.p.load.i.patch_type = (spop.p.load.i.patch_type + PatchOptCNT - 1) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.load.i.ingame_menu_enabled = !spop.p.load.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.load.i.use_dsaving = !spop.p.load.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.load.i.rtc_patch_enabled = !spop.p.load.i.rtc_patch_enabled;
    }

    // Handle the different cases where the user attempts to select an invalid option.
    if (!spop.p.load.i.patches_cache_found && spop.p.load.i.patch_type == PatchEngine)
      spop.p.load.i.patch_type = PatchDatabase;  // Might be invalid, handled below.
    if (!spop.p.load.i.patches_datab_found && spop.p.load.i.patch_type == PatchDatabase)
      spop.p.load.i.patch_type = PatchNone;

    if (!dirsav_avail_sdram(&spop.p.load.i))
      spop.p.load.i.use_dsaving = false;

    // DirSav forces automatic saving
    if (spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_save_type = SaveDirect;
    else if (spop.p.load.l.sram_save_type == SaveDirect)
      spop.p.load.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.load.l.sram_load_type == SaveLoadDisable && spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.load.l.sram_load_type == SaveLoadSav && !spop.p.load.l.savefile_found)
      spop.p.load.l.sram_load_type = spop.p.load.i.use_dsaving ? SaveLoadReset : SaveLoadDisable;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (spop.submenu == GbaLoadPopLoadS) {
      if (spop.selector == GBALdSetCheats)
        spop.p.load.l.use_cheats = !spop.p.load.l.use_cheats;
      if (spop.p.load.i.use_dsaving) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.load.l.sram_load_type = (spop.p.load.l.sram_load_type + 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.load.l.sram_save_type = (spop.p.load.l.sram_save_type + 1) % SaveCNT;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch) {
      if (spop.selector == GBALoadPatch)
        spop.p.load.i.patch_type = (spop.p.load.i.patch_type + 1) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.load.i.ingame_menu_enabled = !spop.p.load.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.load.i.use_dsaving = !spop.p.load.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.load.i.rtc_patch_enabled = !spop.p.load.i.rtc_patch_enabled;
    }

    // If the database has no entry, then do not let the user select that mode.
    if (!spop.p.load.i.patches_datab_found && spop.p.load.i.patch_type == PatchDatabase)
      spop.p.load.i.patch_type = PatchEngine;  // Might be invalid, handled below.
    if (!spop.p.load.i.patches_cache_found && spop.p.load.i.patch_type == PatchEngine)
      spop.p.load.i.patch_type = PatchNone;

    if (!dirsav_avail_sdram(&spop.p.load.i))
      spop.p.load.i.use_dsaving = false;

    // DirSav forces automatic saving
    if (spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_save_type = SaveDirect;
    else if (spop.p.load.l.sram_save_type == SaveDirect)
      spop.p.load.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.load.l.sram_load_type == SaveLoadDisable && spop.p.load.i.use_dsaving)
      spop.p.load.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.load.l.sram_load_type == SaveLoadSav && !spop.p.load.l.savefile_found)
      spop.p.load.l.sram_load_type = SaveLoadReset;
  }

  // Disable ingame-menu if not available.
  if (!ingame_menu_avail_sdram(&spop.p.load.i))
    spop.p.load.i.ingame_menu_enabled = false;

  // If no RTC patches are available, force them to false.
  if (!rtcemu_avail(&spop.p.load.i))
    spop.p.load.i.rtc_patch_enabled = false;

  // Disable cheat loading if no cheats are avail, or IGM is disabled
  if (!spop.p.load.l.cheats_found || !spop.p.load.i.ingame_menu_enabled)
    spop.p.load.l.use_cheats = false;

  if (newkeys & KEY_BUTTA) {
    if (spop.submenu == GbaLoadPopLoadS && spop.selector == GBALdSetRTC && spop.p.load.i.rtc_patch_enabled) {
      void accept_rtc() {
        spop.p.load.l.rtcval = date2timestamp(&spop.rtcpop.val);
      }
      if (spop.p.load.i.rtc_patch_enabled) {
        timestamp2date(spop.p.load.l.rtcval, &spop.rtcpop.val);
        spop.rtcpop.callback = accept_rtc;
      }
    }
    else if (spop.submenu == GbaLoadPopPatch && spop.selector == GBAPatchGen) {
      generate_patches_progress(spop.p.load.i.romfn, spop.p.load.i.romfs);
      spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
      // Try/Load the just-generated patches.
      spop.p.load.i.patches_cache_found = load_cached_patches(spop.p.load.i.romfn, &spop.p.load.i.patches_cache);
    }
    else if (spop.submenu == GbaLoadPopLoadS && spop.selector == GBALdRemember) {
      // Save settings to disk now!
      t_rom_load_settings ld_sett = {
        .patch_policy = spop.p.load.i.patch_type,
        .use_igm = spop.p.load.i.ingame_menu_enabled,
        .use_rtc = spop.p.load.i.rtc_patch_enabled,
        .use_dsaving = spop.p.load.i.use_dsaving
      };
      t_rom_launch_settings lh_sett = {
        .use_cheats = spop.p.load.l.use_cheats,
        .rtcts = spop.p.load.l.rtcval
      };

      save_rom_settings(spop.p.load.i.romfn, &ld_sett, &lh_sett);
      spop.alert_msg = msgs[lang_id][MSG_REMEMB_CFG_OK];
    }
    else if (GbaLoadPopInfo == spop.submenu) {
      // Insert the ROM into the recent list (or move it around). Flush to disk!
      if (recent_menu)
        insert_recent_flush(spop.p.load.i.romfn, FLAG_RECENT_SD);

      // Honor load.patch_type.
      const t_patch *p = get_game_patch(&spop.p.load.i);
      EnumSavetype st = p ? p->save_mode : SaveTypeNone;

      // Prepare the savegame (load and store stuff, directsave...)
      t_dirsave_info dsinfo;
      unsigned errsave = prepare_savegame(
        spop.p.load.l.sram_load_type, spop.p.load.l.sram_save_type,
        st, &dsinfo, spop.p.load.l.savefn);
      if (errsave) {
        unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                          (errsave == ERR_SAVE_CANTALLOC) ? MSG_ERR_SAVEPR :
                          (errsave == ERR_SAVE_BADARG)    ? MSG_ERR_SAVEIT :
                                                            MSG_ERR_SAVEWR;
        spop.alert_msg = msgs[lang_id][errmsg];
        return;
      }

      t_rtc_info rtci = {
        .timestamp = spop.p.load.l.rtcval,
        .ts_step = rtcspeed_default
      };

      unsigned err = load_gba_rom(
        spop.p.load.i.romfn, spop.p.load.i.romfs,
        spop.p.load.l.sram_save_type == SaveDisable ? NULL : spop.p.load.l.savefn, p,
        spop.p.load.l.sram_save_type == SaveDirect ? &dsinfo : NULL,
        spop.p.load.i.ingame_menu_enabled,
        spop.p.load.i.rtc_patch_enabled ? &rtci : NULL,
        spop.p.load.l.use_cheats ? spop.p.load.l.cheats_size : 0,
        loadrom_progress);
      if (err) {
        // Show any errors that might have happened!
        spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
        // TODO: We cannot (in many cases) continue since we trash the SDRAM!
      }
    }
  }

  if (psel != spop.selector)
    spop.anim = 0;
}

static void keypress_popup_savefile(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(SavMAX, spop.selector + 1);

  if (newkeys & KEY_BUTTA) {
    switch (spop.selector) {
    case SaveWrite:
      if (write_save_sram(spop.p.savopt.savfn))
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG0];
      else
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_WERR];
      break;
    case SavLoad:
      if (load_save_sram(spop.p.savopt.savfn))
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG1];
      else
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_RERR];
      break;
    case SavClear:
      if (wipe_sav_file(spop.p.savopt.savfn))
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG2];
      else
        spop.alert_msg = msgs[lang_id][MSG_SAVOPT_MSG_WERR];
      break;
    case SavQuit:
      spop.pop_num = 0;
      break;
    };
  }
}

static void keypress_popup_flash(unsigned newkeys) {
  if ((curr_pressed_keys() & FLASH_GO_KEYS) == FLASH_GO_KEYS)
    start_flash_update(spop.p.update.fn, spop.p.update.fw_size, spop.p.update.issfw);
}

#ifdef SUPPORT_NORGAMES
static void keypress_popup_norwrite(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(GBAPatchCNT - 1, spop.selector + 1);

  if (spop.submenu == GbaNorWrPatch) {
    if (newkeys & (KEY_BUTTLEFT|KEY_BUTTRIGHT)) {
      if (spop.selector == GBALoadPatch)
        spop.p.norwr.i.patch_type = (spop.p.norwr.i.patch_type +
                                     ((newkeys & KEY_BUTTRIGHT) ? 1 : PatchOptCNT - 1)) % PatchOptCNT;
      else if (spop.selector == GBAInGameMen)
        spop.p.norwr.i.ingame_menu_enabled = !spop.p.norwr.i.ingame_menu_enabled;
      else if (spop.selector == GBASavePatch)
        spop.p.norwr.i.use_dsaving = !spop.p.norwr.i.use_dsaving;
      else if (spop.selector == GBARTCPatch)
        spop.p.norwr.i.rtc_patch_enabled = !spop.p.norwr.i.rtc_patch_enabled;
    }

    if (newkeys & KEY_BUTTLEFT) {
      // Handle the different cases where the user attempts to select an invalid option.
      if (!spop.p.norwr.i.patches_cache_found && spop.p.norwr.i.patch_type == PatchEngine)
        spop.p.norwr.i.patch_type = PatchDatabase;  // Might be invalid, handled below.
      if (!spop.p.norwr.i.patches_datab_found && spop.p.norwr.i.patch_type == PatchDatabase)
        spop.p.norwr.i.patch_type = PatchNone;
    }
    if (newkeys & KEY_BUTTRIGHT) {
      // If the database has no entry, then do not let the user select that mode.
      if (!spop.p.norwr.i.patches_datab_found && spop.p.norwr.i.patch_type == PatchDatabase)
        spop.p.norwr.i.patch_type = PatchEngine;  // Might be invalid, handled below.
      if (!spop.p.norwr.i.patches_cache_found && spop.p.norwr.i.patch_type == PatchEngine)
        spop.p.norwr.i.patch_type = PatchNone;
    }

    // Disable certain features (depends on patch types)
    if (!dirsav_avail_flash(&spop.p.norwr.i))
      spop.p.norwr.i.use_dsaving = false;
    if (!ingame_menu_avail_flash(&spop.p.norwr.i))
      spop.p.norwr.i.ingame_menu_enabled = false;
    if (!rtcemu_avail(&spop.p.norwr.i))
      spop.p.norwr.i.rtc_patch_enabled = false;

    if ((newkeys & KEY_BUTTA) && spop.selector == GBAPatchGen) {
      generate_patches_progress(spop.p.norwr.i.romfn, spop.p.norwr.i.romfs);
      spop.alert_msg = msgs[lang_id][MSG_PATCHGEN_OK];
      // Try/Load the just-generated patches.
      spop.p.norwr.i.patches_cache_found = load_cached_patches(spop.p.norwr.i.romfn, &spop.p.norwr.i.patches_cache);
    }
  } else {
    if (newkeys & KEY_BUTTA) {
      // Check whether we have enough space.
      unsigned blkcnt = (spop.p.norwr.i.romfs + NOR_BLOCK_SIZE - 1) / NOR_BLOCK_SIZE;
      if (smenu.fbrowser.freeblks < blkcnt || smenu.fbrowser.maxentries + 1 >= FLASHG_MAXFN_CNT)
        spop.alert_msg = msgs[lang_id][MSG_ERR_NORSPC];
      else {
        const t_load_gba_info *info = &spop.p.norwr.i;
        const t_patch *p = get_game_patch(info);

        // Allocate the last entry for the new game.
        t_flash_game_entry ne = {
          .gamecode = *(uint32_t*)info->romh.gcode,
          .gamever = info->romh.version,
          .numblks = blkcnt,
          .gattrs = (info->use_dsaving         ? GATTR_SAVEDS : 0) |
                    (info->ingame_menu_enabled ? GATTR_IGM    : 0) |
                    (info->rtc_patch_enabled   ? GATTR_RTC    : 0) |
                    GATTR_SAVEM(p),
          .bnoffset = (uint8_t)(file_basename(info->romfn) - info->romfn),
          .entry_addr = ROM_ENTRYPOINT(info->romh)
        };
        memset(&ne.blkmap, 0, sizeof(ne.blkmap));
        strcpy(ne.game_name, info->romfn);

        flashmgr_allocate_blocks(ne.blkmap, blkcnt, (t_reg_entry*)&sdr_state->nordata);

        // Go ahead and start the flasher-loader with patching support.
        unsigned errc = flash_gba_nor(info->romfn, info->romfs, &info->romh, p,
                                      info->use_dsaving, info->ingame_menu_enabled,
                                      info->rtc_patch_enabled,
                                      ne.blkmap, loadrom_progress,
                                      sdr_state->scratch, scratch_mem_size);
        if (errc)
          spop.alert_msg = msgs[lang_id][errc == ERR_LOAD_BADROM ? MSG_ERR_READ : MSG_ERR_NORUPD];
        else {
          // Now we can just write the metadata entry!
          memcpy32(&sdr_state->nordata.games[smenu.fbrowser.maxentries], &ne, sizeof(ne));
          sdr_state->nordata.gamecnt++;
          if (!flashmgr_store(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
            spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];
          else {
            spop.alert_msg = msgs[lang_id][MSG_NOR_WROK];
            spop.pop_num = POPUP_NONE;
          }
        }

        flashbrowser_reload();
      }
    }
  }
}

static void keypress_popup_norload(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(GBALdSetCNT - 1, spop.selector + 1);

  const t_flash_game_entry *e = spop.p.norld.e;
  bool uses_dsave = e->gattrs & GATTR_SAVEDS;
  bool uses_igm   = e->gattrs & GATTR_IGM;
  bool uses_rtc   = e->gattrs & GATTR_RTC;

  if (newkeys & KEY_BUTTLEFT) {
    if (spop.submenu == GbaNorLoad) {
      if (spop.selector == GBALdSetCheats)
        spop.p.norld.l.use_cheats = !spop.p.norld.l.use_cheats;
      if (uses_dsave) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + SaveLoadDSCNT - 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + SaveLoadCNT - 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.norld.l.sram_save_type = (spop.p.norld.l.sram_save_type + SaveCNT - 1) % SaveCNT;
      }
    }

    // DirSav forces automatic saving
    if (uses_dsave)
      spop.p.norld.l.sram_save_type = SaveDirect;
    else if (spop.p.norld.l.sram_save_type == SaveDirect)
      spop.p.norld.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.norld.l.sram_load_type == SaveLoadDisable && uses_dsave)
      spop.p.norld.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.norld.l.sram_load_type == SaveLoadSav && !spop.p.norld.l.savefile_found)
      spop.p.norld.l.sram_load_type = uses_dsave ? SaveLoadReset : SaveLoadDisable;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (spop.submenu == GbaNorLoad) {
      if (spop.selector == GBALdSetCheats)
        spop.p.norld.l.use_cheats = !spop.p.norld.l.use_cheats;
      if (uses_dsave) {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + 1) % SaveLoadDSCNT;
      } else {
        if (spop.selector == GBALdSetLoadP)
          spop.p.norld.l.sram_load_type = (spop.p.norld.l.sram_load_type + 1) % SaveLoadCNT;
        else if (spop.selector == GBALdSetSaveP)
          spop.p.norld.l.sram_save_type = (spop.p.norld.l.sram_save_type + 1) % SaveCNT;
      }
    }

    // DirSav forces automatic saving
    if (uses_dsave)
      spop.p.norld.l.sram_save_type = SaveDirect;
    else if (spop.p.norld.l.sram_save_type == SaveDirect)
      spop.p.norld.l.sram_save_type = autosave_default ? SaveReboot : SaveDisable;

    // If DS is selected, do not allow manual mode.
    if (spop.p.norld.l.sram_load_type == SaveLoadDisable && uses_dsave)
      spop.p.norld.l.sram_load_type = SaveLoadSav;
    // If no .sav is available, do not allow that option!
    if (spop.p.norld.l.sram_load_type == SaveLoadSav && !spop.p.norld.l.savefile_found)
      spop.p.norld.l.sram_load_type = SaveLoadReset;
  }

  // Disable cheat loading if no cheats are avail, or IGM is disabled
  if (!spop.p.norld.l.cheats_found || !uses_igm)
    spop.p.norld.l.use_cheats = false;

  if (newkeys & KEY_BUTTA) {
    if (spop.submenu == GbaLoadPopInfo) {
      const int stype = GET_GATTR_SAVEM(e->gattrs);
      const EnumSavetype st = stype < 0 ? SaveTypeNone : stype;
      bool uses_dsave = e->gattrs & GATTR_SAVEDS;
      bool uses_igm   = e->gattrs & GATTR_IGM;
      bool uses_rtc   = e->gattrs & GATTR_RTC;

      t_dirsave_info dsinfo;
      unsigned errsave = prepare_savegame(
        spop.p.norld.l.sram_load_type, spop.p.norld.l.sram_save_type,
        st, &dsinfo, spop.p.norld.l.savefn);
      if (errsave) {
        unsigned errmsg = (errsave == ERR_SAVE_BADSAVE)   ? MSG_ERR_SAVERD :
                          (errsave == ERR_SAVE_CANTALLOC) ? MSG_ERR_SAVEPR :
                          (errsave == ERR_SAVE_BADARG)    ? MSG_ERR_SAVEIT :
                                                            MSG_ERR_SAVEWR;
        spop.alert_msg = msgs[lang_id][errmsg];
        return;
      }
      t_rtc_info rtci = {
        .timestamp = spop.p.norld.l.rtcval,
        .ts_step = rtcspeed_default
      };

      if (recent_menu)
        insert_recent_flush(e->game_name, FLAG_RECENT_NOR);

      // TODO Handle errors, finish missing stuff.
      unsigned err = launch_gba_nor(
        e->game_name,
        spop.p.norld.l.sram_save_type == SaveDisable ? NULL : spop.p.norld.l.savefn,
        e->blkmap, e->numblks,
        uses_dsave ? &dsinfo : NULL,
        uses_rtc ? &rtci : NULL,
        uses_igm,
        spop.p.norld.l.use_cheats ? spop.p.norld.l.cheats_size : 0);
    }
    else if (spop.selector == GBALdRemember) {
      // Save settings to disk now!
      t_rom_load_settings ld_sett = {  // Use defaults in case it doesn't really exist
        .patch_policy = patcher_default,
        .use_igm = ingamemenu_default,
        .use_rtc = rtcpatch_default,
        .use_dsaving = autosave_prefer_ds
      };
      t_rom_launch_settings lh_sett = {
        .use_cheats = spop.p.norld.l.use_cheats,
        .rtcts = spop.p.norld.l.rtcval
      };

      // We load the loading settings to ensure we do not overwrite them.
      load_rom_settings(e->game_name, &ld_sett, NULL);
      save_rom_settings(e->game_name, &ld_sett, &lh_sett);
      spop.alert_msg = msgs[lang_id][MSG_REMEMB_CFG_OK];
    }
    else if (spop.selector == GBALdSetRTC) {
      void accept_rtc() {
        spop.p.norld.l.rtcval = date2timestamp(&spop.rtcpop.val);
      }
      if (uses_rtc) {
        timestamp2date(spop.p.norld.l.rtcval, &spop.rtcpop.val);
        spop.rtcpop.callback = accept_rtc;
      }
    }
  }
}
#endif

static void keypress_popup_filemgr(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    spop.selector = MAX(0, spop.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    spop.selector = MIN(FiMgrCNT - 1, spop.selector + 1);

  if (newkeys & KEY_BUTTA) {
    t_centry *e = sdr_state->fileorder[smenu.browser.selector];
    switch (spop.selector) {
    case FiMgrDelete:
      {
        void remove_file_action(bool confirm) {
          char tmpfn[MAX_FN_LEN];
          strcpy(tmpfn, smenu.browser.cpath);
          strcat(tmpfn, sdr_state->fileorder[smenu.browser.selector]->fname);

          if (confirm) {
            if (FR_OK != f_unlink(tmpfn))
              spop.alert_msg = msgs[lang_id][MSG_ERR_DELFILE];
            else
              spop.alert_msg = msgs[lang_id][MSG_OK_DELFILE];

            browser_reload();   // Force reload so the file disappears!
          }
        }
        spop.qpop.message = msgs[lang_id][MSG_Q0_DELFILE];
        spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
        spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
        spop.qpop.option = 0;
        spop.qpop.callback = remove_file_action;
        spop.qpop.clear_popup_ok = true;
      }
      break;
    case FiMgrHide:
      {
        char tmpfn[MAX_FN_LEN];
        strcpy(tmpfn, smenu.browser.cpath);
        strcat(tmpfn, sdr_state->fileorder[smenu.browser.selector]->fname);

        if (FR_OK == f_chmod(tmpfn, e->attr ^ AM_HID, AM_HID))
          e->attr ^= AM_HID;
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      }
      spop.pop_num = POPUP_NONE;
      break;

    #ifdef SUPPORT_NORGAMES
    case FiMgrWriteNOR:
      if (e->filesize > MAX_GBA_ROM_SIZE)
        spop.alert_msg = msgs[lang_id][MSG_ERR_TOOBIG];
      else {
        char path[MAX_FN_LEN];
        strcpy(path, smenu.browser.cpath);
        strcat(path, e->fname);

        // Load default loading settings if any.
        t_rom_load_settings ld_sett = {
          .patch_policy = patcher_default,
          .use_igm = ingamemenu_default,
          .use_rtc = rtcpatch_default,
          .use_dsaving = autosave_prefer_ds
        };
        load_rom_settings(path, &ld_sett, NULL);

        if (!prepare_gba_info(&spop.p.norwr.i, &ld_sett, path, e->filesize, false))
          spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
        else {
          spop.pop_num = POPUP_GBA_NORWRITE;
          spop.submenu = GbaLoadPopInfo;
          spop.selector = 0;
        }
      }
      break;
    #endif
    };
  }
}

static void keypress_menu_recent(unsigned newkeys) {
  if (smenu.recent.maxentries) {
    if (newkeys & KEY_BUTTUP)
      smenu.recent.selector = MAX(0, smenu.recent.selector - 1);
    else if (newkeys & KEY_BUTTDOWN)
      smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector + 1);
    if (newkeys & KEY_BUTTLEFT) {
      smenu.recent.selector = MAX(0, smenu.recent.selector - RECENT_ROWS);
      smenu.recent.seloff   = MAX(0, smenu.recent.seloff - RECENT_ROWS);
    }
    else if (newkeys & KEY_BUTTRIGHT) {
      smenu.recent.selector = MIN(smenu.recent.maxentries - 1, smenu.recent.selector + RECENT_ROWS);
      smenu.recent.seloff   = MIN(smenu.recent.maxentries - 1, smenu.recent.seloff   + RECENT_ROWS);
    }
    if (newkeys & KEY_BUTTA) {
      t_rentry *e = &sdr_state->rentries[smenu.recent.selector];
      #ifdef SUPPORT_NORGAMES
      if (e->flags & FLAG_RECENT_NOR) {
        // Try to find the ROM in the current flash metadata.
        for (unsigned i = 0; i < sdr_state->nordata.gamecnt; i++) {
          const t_flash_game_entry *fe = &sdr_state->nordata.games[i];
          if (!strcmp(e->fpath, fe->game_name)) {
            browser_open_nor(fe);
            return;
          }
        }
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      }
      else
      #endif
      {
        // stat() the file since we need the size, and validate that it exists!
        FILINFO info;
        FRESULT res = f_stat(e->fpath, &info);
        if (res == FR_OK)
          browser_open(e->fpath, info.fsize);
        else
          spop.alert_msg = msgs[lang_id][MSG_ERR_READ];
      }
    }
    else if (newkeys & KEY_BUTTSEL) {
      void recent_del_cb(bool confirm) {
        if (confirm)
          delete_recent_flush(smenu.recent.selector);
      }
      spop.qpop.message = msgs[lang_id][MSG_Q4_DELREC];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = recent_del_cb;
      spop.qpop.clear_popup_ok = false;
    }
  }

  if (smenu.recent.selector < smenu.recent.seloff)
    smenu.recent.seloff = smenu.recent.selector;
  else if (smenu.recent.selector >= smenu.recent.seloff + RECENT_ROWS)
    smenu.recent.seloff = smenu.recent.selector - RECENT_ROWS + 1;
}

// Keys for the carousel. LEFT/RIGHT move one entry and UP/DOWN a few. A or
// START (the label on the selected entry) open the entry (enter directories,
// launch files), B goes to the parent directory and SELECT
// shows the file management menu, like in the plain ROM browser.
static void keypress_menu_browse(unsigned newkeys) {
  const int n = smenu.browser.dispentries;
  if (n) {
    int sel = smenu.browser.selector;
    if (newkeys & KEY_BUTTLEFT)
      sel = MAX(0, sel - 1);
    if (newkeys & KEY_BUTTRIGHT)
      sel = MIN(n - 1, sel + 1);
    if (newkeys & KEY_BUTTUP)
      sel = MAX(0, sel - CAR_PAGE);
    if (newkeys & KEY_BUTTDOWN)
      sel = MIN(n - 1, sel + CAR_PAGE);
    smenu.browser.selector = sel;

    // Move into a new dir and/or open a file
    if (newkeys & (KEY_BUTTA | KEY_BUTTSTA)) {
      t_centry *e = sdr_state->fileorder[sel];
      if (strlen(smenu.browser.cpath) + strlen(e->fname) + 2 > MAX_FN_LEN) {
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      }
      else if (e->isdir) {
        strcat(smenu.browser.cpath, e->fname);
        strcat(smenu.browser.cpath, "/");
        // Push selector history and reset it in the new dir
        memmove(&smenu.browser.selhist[1], &smenu.browser.selhist[0],
                sizeof(smenu.browser.selhist) - sizeof(smenu.browser.selhist[0]));
        smenu.browser.selhist[0] = sel;
        smenu.browser.selector = 0;
        browser_reload();
        car_pos = smenu.browser.selector << 8;     // No scrolling across directories
      } else {
        char path[MAX_FN_LEN];
        strcpy(path, smenu.browser.cpath);
        strcat(path, e->fname);
        browser_open(path, e->filesize);
      }
    }
    else if (newkeys & KEY_BUTTSEL) {
      // Shows a file management menu.
      spop.pop_num = POPUP_FILE_MGR;
      spop.anim = 0;
      spop.selector = 0;
    }
  }
  if (newkeys & KEY_BUTTB) {
    // Try to go up in the dir structure
    if (movedir_up()) {
      smenu.browser.selector = smenu.browser.selhist[0];
      memmove(&smenu.browser.selhist[0], &smenu.browser.selhist[1],
              sizeof(smenu.browser.selhist) - sizeof(smenu.browser.selhist[0]));
      browser_reload();
      car_pos = smenu.browser.selector << 8;
    } else {
      smenu.menu_tab = MENUTAB_HOME;
    }
  }
}

// Clock tab: A sets the time.
static void keypress_menu_clock(unsigned newkeys) {
  if (newkeys & KEY_BUTTA)
    clock_open_setter();
}

// Home screen: D-pad moves the selection, A activates the button.
static void home_activate(int b) {
  switch (b) {
  case HB_FLASH:
    #ifdef SUPPORT_NORGAMES
    smenu.menu_tab = MENUTAB_NORBROWSE;
    #else
    spop.alert_msg = "No flash games on this cart";
    #endif
    break;
  case HB_RECENT:
    if (recent_available())
      smenu.menu_tab = MENUTAB_RECENT;
    else
      spop.alert_msg = "No recent games";
    break;
  case HB_HWTEST:
    smenu.menu_tab = MENUTAB_TOOLS;
    break;
  case HB_CAROUSEL:
    smenu.menu_tab = MENUTAB_ROMBROWSE;
    break;
  case HB_THEME:
    menu_theme = (menu_theme == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
    reload_theme(menu_theme);
    save_ui_settings();
    break;
  case HB_SETTINGS:
    smenu.menu_tab = MENUTAB_SETTINGS;
    break;
  case HB_TIME:
    clock_open_setter();
    break;
  }
}

static void keypress_menu_home(unsigned newkeys) {
  static const uint16_t dirkeys[4] = { KEY_BUTTUP, KEY_BUTTDOWN, KEY_BUTTLEFT, KEY_BUTTRIGHT };
  const int src = smenu.home.sel;
  for (unsigned d = 0; d < 4; d++) {
    if (!(newkeys & dirkeys[d]) || home_nav[src][d] < 0)
      continue;
    int dst = home_nav[src][d];
    // Moving vertically through the middle row returns to the last used button.
    if (dst == HB_RECENT && (src == HB_FLASH || src == HB_CAROUSEL))
      dst = smenu.home.row2;
    smenu.home.sel = dst;
    if (dst == HB_RECENT || dst == HB_HWTEST)
      smenu.home.row2 = dst;
    break;
  }

  if (newkeys & KEY_BUTTA)
    home_activate(smenu.home.sel);
}

#ifdef SUPPORT_NORGAMES
static void keypress_menu_norbrowse(unsigned newkeys) {
  if (smenu.fbrowser.maxentries) {
    if (newkeys & KEY_BUTTUP)
      smenu.fbrowser.selector = MAX(0, smenu.fbrowser.selector - 1);
    if (newkeys & KEY_BUTTDOWN)
      smenu.fbrowser.selector = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.selector + 1);
    if (newkeys & KEY_BUTTLEFT) {
      smenu.fbrowser.selector = MAX(0, smenu.fbrowser.selector - NORGAMES_ROWS);
      smenu.fbrowser.seloff   = MAX(0, smenu.fbrowser.seloff - NORGAMES_ROWS);
    }
    if (newkeys & KEY_BUTTRIGHT) {
      smenu.fbrowser.selector = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.selector + NORGAMES_ROWS);
      smenu.fbrowser.seloff   = MIN(smenu.fbrowser.maxentries - 1, smenu.fbrowser.seloff   + NORGAMES_ROWS);
    }

    if (newkeys & KEY_BUTTA)
      browser_open_nor(&sdr_state->nordata.games[smenu.fbrowser.selector]);
    else if (newkeys & KEY_BUTTSEL) {
      // Prompt NOR entry deletion.
      void remove_nor_action(bool confirm) {
        if (!confirm)
          return;

        // Remove game entry, just memmove the other games on top.
        sdr_state->nordata.gamecnt--;
        memmove32(&sdr_state->nordata.games[smenu.fbrowser.selector],
                  &sdr_state->nordata.games[smenu.fbrowser.selector + 1],
                  (sdr_state->nordata.gamecnt - smenu.fbrowser.selector) * sizeof(t_flash_game_entry));

        // Go ahead and write a new metadata entry;
        if (!flashmgr_store(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE, (t_reg_entry*)&sdr_state->nordata))
          spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];
        flashbrowser_reload();   // Force list reload, free block calculation, etc.
      }
      spop.qpop.message = msgs[lang_id][MSG_Q5_DELNORG];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = remove_nor_action;
      spop.qpop.clear_popup_ok = true;
    }

    if (smenu.fbrowser.selector < smenu.fbrowser.seloff)
      smenu.fbrowser.seloff = smenu.fbrowser.selector;
    else if (smenu.fbrowser.selector >= smenu.fbrowser.seloff + NORGAMES_ROWS)
      smenu.fbrowser.seloff = smenu.fbrowser.selector - NORGAMES_ROWS + 1;
  }
}
#endif

static void keypress_menu_settings(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    smenu.set.selector = MAX(0, smenu.set.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    smenu.set.selector = MIN(SettMAX - 1, smenu.set.selector + 1);
  if (newkeys & KEY_BUTTLEFT) {
    if (smenu.set.selector == SettHotkey)
      hotkey_combo = (hotkey_combo + hotkey_listcnt - 1) % hotkey_listcnt;
    else if (smenu.set.selector == SettSaveLoc)
      save_path_default = (save_path_default + SaveDirCNT - 1) % SaveDirCNT;
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettSaveLocNOR)
      save_path_nor_default = (save_path_nor_default + SaveDirNORCNT - 1) % SaveDirNORCNT;
    #endif
    else if (smenu.set.selector == SettStateLoc)
      state_path_default = (state_path_default + StateDirCNT - 1) % StateDirCNT;
    else if (smenu.set.selector == SettSaveBkp)
      backup_sram_default = backup_sram_default ? backup_sram_default - 1 : 0;
    else if (smenu.set.selector == DefsPatchEng)
      patcher_default = (patcher_default + PatchTotalCNT - 1) % PatchTotalCNT;
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + RTC_SPEED_CNT - 1) % RTC_SPEED_CNT;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (smenu.set.selector == SettHotkey)
      hotkey_combo = (hotkey_combo + 1) % hotkey_listcnt;
    else if (smenu.set.selector == SettSaveLoc)
      save_path_default = (save_path_default + 1) % SaveDirCNT;
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettSaveLocNOR)
      save_path_nor_default = (save_path_nor_default + 1) % SaveDirNORCNT;
    #endif
    else if (smenu.set.selector == SettStateLoc)
      state_path_default = (state_path_default + 1) % StateDirCNT;
    else if (smenu.set.selector == SettSaveBkp)
      backup_sram_default = MIN(MAX_BACKUP_CNT, backup_sram_default + 1);
    else if (smenu.set.selector == DefsPatchEng)
      patcher_default = (patcher_default + 1) % PatchTotalCNT;
    else if (smenu.set.selector == DefsRTCSpeed)
      rtcspeed_default = (rtcspeed_default + 1) % RTC_SPEED_CNT;
  }
  if (newkeys & (KEY_BUTTLEFT | KEY_BUTTRIGHT)) {
    if (smenu.set.selector == SettBootType)
      boot_bios_splash ^= 1;
    else if (smenu.set.selector == SettCheatEn)
      enable_cheats ^= 1;
    else if (smenu.set.selector == DefsGamMenu)
      ingamemenu_default ^= 1;
    else if (smenu.set.selector == DefsRTCEnb)
      rtcpatch_default ^= 1;
    else if (smenu.set.selector == DefsLoadPol)
      autoload_default ^= 1;
    else if (smenu.set.selector == DefsSavePol)
      autosave_default ^= 1;
    else if (smenu.set.selector == DefsPrefDS)
      autosave_prefer_ds ^= 1;
    else if (smenu.set.selector == SettFastSD)
      use_slowld ^= 1;
    else if (smenu.set.selector == SettFastEWRAM)
      use_fastew = fastew ? (use_fastew ^ 1) : 0;
    #ifdef SUPPORT_NORGAMES
    else if (smenu.set.selector == SettVerifyNOR)
      use_verify_nor ^= 1;
    #endif
  }

  if (newkeys & KEY_BUTTA && smenu.set.selector == DefsRTCVal) {
    void accept_rtc() {
      rtcvalue_default = date2timestamp(&spop.rtcpop.val);
    }
    timestamp2date(rtcvalue_default, &spop.rtcpop.val);
    spop.rtcpop.callback = accept_rtc;
  }
  if (newkeys & KEY_BUTTA && smenu.set.selector == SettSave) {
    smenu.set.selector = 0;
    if (save_settings())
      spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
    else
      spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
  }
}

// User name editor: UP/DOWN change the character, LEFT/RIGHT move the cursor,
// A accepts and B cancels.
static void keypress_username_edit(unsigned newkeys) {
  char *c = &user_name[smenu.uiset.cursor];
  const int n = sizeof(name_chars) - 1;
  const char *p = strchr(name_chars, *c);
  int idx = (p && *c) ? (int)(p - name_chars) : 0;
  if (newkeys & KEY_BUTTUP)
    idx = (idx + 1) % n;
  if (newkeys & KEY_BUTTDOWN)
    idx = (idx + n - 1) % n;
  *c = name_chars[idx];
  if ((newkeys & KEY_BUTTLEFT) && smenu.uiset.cursor)
    smenu.uiset.cursor--;
  if ((newkeys & KEY_BUTTRIGHT) && smenu.uiset.cursor < USERNAME_MAX - 1)
    smenu.uiset.cursor++;

  if (newkeys & (KEY_BUTTA | KEY_BUTTB)) {
    if (newkeys & KEY_BUTTB)
      strcpy(user_name, smenu.uiset.oldname);
    else {
      unsigned len = USERNAME_MAX;
      while (len && user_name[len - 1] == ' ')
        len--;
      user_name[len] = 0;
    }
    smenu.uiset.editing = 0;
  }
}

static void keypress_menu_uisettings(unsigned newkeys) {
  if (smenu.uiset.editing) {
    keypress_username_edit(newkeys);
    return;
  }
  if (newkeys & KEY_BUTTUP)
    smenu.uiset.selector = MAX(0, smenu.uiset.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    smenu.uiset.selector = MIN(UiSetMAX, smenu.uiset.selector + 1);
  if (newkeys & KEY_BUTTLEFT) {
    if (smenu.uiset.selector == UiSetTheme)
      menu_theme = menu_theme ? menu_theme - 1 : 0;
    else if (smenu.uiset.selector == UiSetASpd)
      anim_speed = anim_speed ? anim_speed - 1 : 0;
    else if (smenu.uiset.selector == UiSetHid)
      hide_hidden ^= 1;
    else if (smenu.uiset.selector == UiSetRect)
      recent_menu ^= 1;
    else if (smenu.uiset.selector == UiSetLang)
      lang_id = (lang_id + LANG_COUNT - 1) % LANG_COUNT;
    else if (smenu.uiset.selector == UiSetAccent)
      accent_color = (accent_color + ACCENT_COUNT - 1) % ACCENT_COUNT;
  }
  if (newkeys & KEY_BUTTRIGHT) {
    if (smenu.uiset.selector == UiSetTheme)
      menu_theme = MIN(THEME_COUNT - 1, menu_theme + 1);
    else if (smenu.uiset.selector == UiSetASpd)
      anim_speed = MIN(animspd_cnt - 1, anim_speed + 1);
    else if (smenu.uiset.selector == UiSetHid)
      hide_hidden ^= 1;
    else if (smenu.uiset.selector == UiSetRect)
      recent_menu ^= 1;
    else if (smenu.uiset.selector == UiSetLang)
      lang_id = (lang_id + 1) % LANG_COUNT;
    else if (smenu.uiset.selector == UiSetAccent)
      accent_color = (accent_color + 1) % ACCENT_COUNT;
  }

  if (newkeys & KEY_BUTTA && smenu.uiset.selector == UiSetName) {
    // Start editing: pad the name with spaces to its full length.
    strcpy(smenu.uiset.oldname, user_name);
    for (unsigned i = strlen(user_name); i < USERNAME_MAX; i++)
      user_name[i] = ' ';
    user_name[USERNAME_MAX] = 0;
    smenu.uiset.cursor = 0;
    smenu.uiset.editing = 1;
  }

  if (newkeys & KEY_BUTTA && smenu.uiset.selector == UiSetSave) {
    smenu.uiset.selector = 0;
    if (save_ui_settings())
      spop.alert_msg = msgs[lang_id][MSG_OK_SETSAVE];
    else
      spop.alert_msg = msgs[lang_id][MSG_ERR_SETSAVE];
  }

  reload_theme(menu_theme);
}

static void keypress_menu_tools(unsigned newkeys) {
  if (newkeys & KEY_BUTTUP)
    smenu.tools.selector = MAX(0, smenu.tools.selector - 1);
  if (newkeys & KEY_BUTTDOWN)
    smenu.tools.selector = MIN(ToolsMAX - 1, smenu.tools.selector + 1);

  if (newkeys & KEY_BUTTA) {
    if (smenu.tools.selector == ToolsSDRAMTest) {
      // Performs a test on the SRAM/SDRAM, ensure they are fine.
      set_supercard_mode(MAPPED_SDRAM, true, false);

      if (sdram_test(loadrom_progress_abort))
        spop.alert_msg = msgs[lang_id][MSG_BAD_SDRAM];
      else
        spop.alert_msg = msgs[lang_id][MSG_GOOD_RAM];

      set_supercard_mode(MAPPED_SDRAM, true, true);
    }
    if (smenu.tools.selector == ToolsSRAMTest) {
      if (sram_test())
        spop.alert_msg = msgs[lang_id][MSG_BAD_SRAM];
      else
        spop.alert_msg = msgs[lang_id][MSG_GOOD_RAM];
    }
    else if (smenu.tools.selector == ToolsBatteryTest) {
      // Go ahead and fill in SRAM with a pattern.
      spop.qpop.message = msgs[lang_id][MSG_Q2_SRAMTST];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = sram_battery_test_callback;
      spop.qpop.clear_popup_ok = true;
    }
    else if (smenu.tools.selector == ToolsSDBench) {
      slowsd = use_slowld;
      int ret = sdbench_read(loadrom_progress_abort);
      slowsd = true;
      if (ret < 0)
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];
      else {
        unsigned speed = 8*1024*1024 / (unsigned)ret;
        npf_snprintf(smenu.info.tstr, sizeof(smenu.info.tstr), msgs[lang_id][MSG_BENCHSPD], speed);
        spop.alert_msg = smenu.info.tstr;
      }
    }
    else if (smenu.tools.selector == ToolsFlashBak) {
      // Backup the flash contents to a file.
      if (dump_flashmem_backup())
        spop.alert_msg = msgs[lang_id][MSG_FLASH_READOK];
      else
        spop.alert_msg = msgs[lang_id][MSG_ERR_GENERIC];

      browser_reload();
    }
    #ifdef SUPPORT_NORGAMES
    else if (smenu.tools.selector == ToolsFlashClr) {
      void flash_clear_callback(bool confirm) {
        if (confirm) {
          // Delete all metadata (data is not really wiped, takes too long)
          if (flashmgr_wipe(ROM_FLASHMETA_ADDR, FLASH_METADATA_SIZE))
            spop.alert_msg = msgs[lang_id][MSG_NOR_CLOK];
          else
            spop.alert_msg = msgs[lang_id][MSG_ERR_NORUPD];

          flashbrowser_reload();     // Ensure we clear the NOR entries from RAM.
        }
      }
      // Prompt the user for clearing the memory.
      spop.qpop.message = msgs[lang_id][MSG_Q6_CLRNOR];
      spop.qpop.default_button = msgs[lang_id][MSG_Q_NO];
      spop.qpop.confirm_button = msgs[lang_id][MSG_Q_YES];
      spop.qpop.option = 0;
      spop.qpop.callback = flash_clear_callback;
      spop.qpop.clear_popup_ok = true;
    }
    #endif
  }
}

static void keypress_menu_info(unsigned newkeys) {
  if (newkeys & KEY_BUTTA)
    smenu.info.selector = (smenu.info.selector + 1) % 4;
  if ((curr_pressed_keys() & FLASH_UNLOCK_KEYS) == FLASH_UNLOCK_KEYS)
    enable_flashing = true;
}


void menu_keypress(unsigned newkeys) {
  if (spop.alert_msg) {
    // Modal message pop up!
    if (newkeys & (KEY_BUTTA | KEY_BUTTB))
      spop.alert_msg = NULL;
  }
  else if (spop.qpop.message) {
    // Modal confirm/question dialog
    if (newkeys & (KEY_BUTTUP | KEY_BUTTDOWN))
      spop.qpop.option ^= 1;
    else if (newkeys & KEY_BUTTB)
      spop.qpop.message = NULL;   // Exit the modal dialog.
    else if (newkeys & KEY_BUTTA) {
      if (spop.qpop.callback) {
        if (spop.qpop.option && spop.qpop.clear_popup_ok)
          spop.pop_num = POPUP_NONE;
        spop.qpop.callback(spop.qpop.option);
      }
      spop.qpop.message = NULL;   // Exit the modal dialog.
    }
  }
  else if (spop.rtcpop.callback) {
    if (newkeys & KEY_BUTTLEFT)
      spop.rtcpop.selector = MAX(0, spop.rtcpop.selector - 1);
    if (newkeys & KEY_BUTTRIGHT)
      spop.rtcpop.selector = MIN(4, spop.rtcpop.selector + 1);

    if (newkeys & KEY_BUTTUP)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector]++;
    if (newkeys & KEY_BUTTDOWN)
      ((uint8_t*)&spop.rtcpop.val)[spop.rtcpop.selector]--;

    if (newkeys & (KEY_BUTTUP|KEY_BUTTDOWN))
      fixdate(&spop.rtcpop.val);

    if (newkeys & KEY_BUTTB) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback = NULL;
      spop.rtcpop.title = NULL;
    }
    else if (newkeys & KEY_BUTTA) {
      spop.rtcpop.selector = 0;
      spop.rtcpop.callback();
      spop.rtcpop.callback = NULL;
      spop.rtcpop.title = NULL;
    }
  }
  else if (spop.pop_num) {
    const int subcnt = popup_windows[spop.pop_num - 1].max_submenu;
    if (newkeys & KEY_BUTTL)
      spop.submenu = (spop.submenu + subcnt - 1) % subcnt;
    if (newkeys & KEY_BUTTR)
      spop.submenu = (spop.submenu + 1) % subcnt;

    // Close pop-up on B button
    if (newkeys & KEY_BUTTB)
      spop.pop_num = 0;
    else {
      const t_mkeyupd_fn keyfns[] = {
        NULL,
        keypress_popup_loadgba,
        keypress_popup_savefile,
        keypress_popup_flash,
        keypress_popup_filemgr,
        #ifdef SUPPORT_NORGAMES
        keypress_popup_norwrite,
        keypress_popup_norload,
        #endif
      };
      keyfns[spop.pop_num](newkeys);
    }
  } else {
    const unsigned tab0 = smenu.menu_tab;
    const bool editing = tab0 == MENUTAB_UILANG && smenu.uiset.editing;

    // The settings tabs (settings, UI settings, about) are switched with the
    // shoulder buttons; anywhere else they toggle the clock tab, going back to
    // the previous tab.
    static const uint8_t setgrp[3] = { MENUTAB_SETTINGS, MENUTAB_UILANG, MENUTAB_INFO };
    int gi = -1;
    for (unsigned i = 0; i < 3; i++)
      if (tab0 == setgrp[i])
        gi = i;
    if (!editing && (newkeys & (KEY_BUTTL | KEY_BUTTR))) {
      if (gi >= 0) {
        if ((newkeys & KEY_BUTTL) && gi > 0)
          smenu.menu_tab = setgrp[gi - 1];
        else if ((newkeys & KEY_BUTTR) && gi < 2)
          smenu.menu_tab = setgrp[gi + 1];
      }
      else if (tab0 == MENUTAB_CLOCK)
        smenu.menu_tab = smenu.prev_tab;
      else {
        smenu.prev_tab = tab0;
        smenu.menu_tab = MENUTAB_CLOCK;
      }
    }

    if (newkeys & (KEY_BUTTL | KEY_BUTTR | KEY_BUTTUP | KEY_BUTTDOWN))
      smenu.anim_state = 0;

    const t_mkeyupd_fn keyfns[] = {
      keypress_menu_home,
      keypress_menu_recent,
      keypress_menu_browse,
      #ifdef SUPPORT_NORGAMES
      keypress_menu_norbrowse,
      #endif
      keypress_menu_clock,
      keypress_menu_settings,
      keypress_menu_uisettings,
      keypress_menu_tools,
      keypress_menu_info,
    };
    keyfns[smenu.menu_tab](newkeys);

    // B goes back to the home tab, unless the tab uses it (browsers) or the
    // tab was just changed.
    if ((newkeys & KEY_BUTTB) && !editing && smenu.menu_tab == tab0 &&
        tab0 != MENUTAB_HOME && tab0 != MENUTAB_ROMBROWSE)
      smenu.menu_tab = MENUTAB_HOME;
  }
}

// Only repeat keys A/B and Dir
const uint16_t keyrep = 0x0F3;
static uint32_t keyreptmr[10] = {0};
static uint8_t  keyrepcnt[10] = {0};
static uint32_t prev_keys = 0;

// Handle button input. Supports key re-press whenever a button is held for a while.
// This key repeat pattern can be tuned for speed and what not.
uint16_t get_keypress() {
  uint32_t ckeys = curr_pressed_keys();
  uint32_t mkeys = 0;
  for (unsigned i = 0; i < 10; i++) {
    if (ckeys & (1 << i)) {
      if (!(prev_keys & (1 << i))) {
        keyreptmr[i] = systime() + KEY_REPEAT_INITIAL;
        mkeys |= (1 << i);
        keyrepcnt[i] = 0;
      }
      else if (((1 << i) & keyrep) && systime() > keyreptmr[i]) {
        if (keyrepcnt[i] > KEY_REPEAT_CNT1)
          keyreptmr[i] = systime() + KEY_REPEAT_FAST;
        else {
          keyreptmr[i] = systime() + KEY_REPEAT_MID;
          keyrepcnt[i]++;
        }
        mkeys |= (1 << i);
      }
    }
    else
      keyreptmr[i] = 0;
  }

  prev_keys = ckeys;
  return mkeys;
}

