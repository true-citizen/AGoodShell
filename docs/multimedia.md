# Media: MP3 playback and picture viewer (experimental, `multimedia` branch)

Real MP3 decoding on the GBA's ARM7TDMI (16.78 MHz, no FPU) is possible, but only for small streams.

## Limits

| Stream | CPU use (mGBA) | Verdict |
|---|---|---|
| 16 kHz mono, 16 kbps | 61% (worst frame 85%) | works |
| 16 kHz mono, 32 kbps | up to 112% over a second, 88% after | plays without underruns, marginal |
| 22.05 kHz mono, 16 kbps | 79% (worst frame 111%) | stutters |
| 22.05 kHz mono, 32 kbps | 89% | too tight |
| 44.1 kHz stereo, 64 kbps | 329% | impossible |

So: **MPEG-1/2 Layer III, mono (stereo is mixed down), 16 kHz or lower, about 16 kbps.**
Output is 8-bit on Direct Sound A (18157 Hz, linear resampling), so expect "old radio" quality.
Encode with e.g. `ffmpeg -i in.wav -ac 1 -ar 16000 -b:a 16k out.mp3`.

## How it works

* `media/` is a standalone ROM (`make mediaplayer.gba`) using libmad (`third_party/libmad`, GPL-2+, compatible with this project's GPLv3).
  Hot decoder code runs from IWRAM (about 19.7 KB), data from EWRAM, constant tables from ROM.
  Code in EWRAM would need 143% CPU, so IWRAM is a must.
* The firmware treats it like an emulator: a `.mp3` file is loaded after the player ROM, behind an `AMP3` header (see `media_header` in `src/emu.c`).
  The ROM is bundled as asset `MEDI` or can be placed at `/.superfw/emulators/mediaplayer.gba`.
* Controls: A pause, B exit to the firmware, Up/Down volume, L/R seek by 5%.
  The screen shows time, CPU load and buffer underruns.

## Pictures (.png, .jpg, .jpeg, .gif)

The same ROM shows pictures (header magic `AIMG`, picked by the file extension). It draws into the
240x160 screen as it decodes, so big pictures appear progressively; B leaves.
Pictures that fit the screen are shown 1:1, bigger ones are scaled down to fit (averaging).

| Format | Supported | Not supported |
|---|---|---|
| PNG | all color types and bit depths, transparency (blended over black) | interlaced (Adam7), width over 4096 |
| JPEG | baseline (picojpeg: gray, 4:4:4, 4:2:2, 4:2:0) | progressive, arithmetic coding |
| GIF | animation for pictures up to 240x160 (integer zoom when smaller), transparency, interlace | animation of bigger pictures (first frame only); "restore previous" disposal is treated as "leave" |

The on-screen text uses Galmuri9 (SIL OFL 1.1, see res/fonts/OFL-Galmuri.txt), like the firmware menu.

Speed (mGBA, hot code in IWRAM): about 25 s for a 1000x700 PNG and about 30 s per megapixel of
JPEG. JPEGs at least 8 times bigger than the screen are decoded at 1/8 size (one pixel per 8x8
block), which is much faster (a 3200x2100 photo takes about 20 s). picojpeg is public domain.

## Testing

`tests/media/mktest.py` builds a ROM (media ROM + file) to run in mGBA, and `tests/media/audio.c` captures its audio and a screenshot with libmgba.

## Status

Verified in mGBA only (correct pitch and timing, 0 underruns on a 16 kHz/16 kbps file; test pictures of each format). **Not tested on real hardware**, including real SD read speed and the exit path.
