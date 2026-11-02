# MP3 playback (experimental, `multimedia` branch)

Real MP3 decoding on the GBA's ARM7TDMI (16.78 MHz, no FPU) is possible, but only for small streams.

## Limits

| Stream | CPU use (mGBA) | Verdict |
|---|---|---|
| 16 kHz mono, 16 kbps | 61% (worst frame 85%) | works |
| 22.05 kHz mono, 16 kbps | 79% (worst frame 111%) | stutters |
| 22.05 kHz mono, 32 kbps | 89% | too tight |
| 44.1 kHz stereo, 64 kbps | 329% | impossible |

So: **MPEG-1/2 Layer III, mono (stereo is mixed down), 16 kHz or lower, about 16 kbps.**
Output is 8-bit on Direct Sound A (18157 Hz, linear resampling), so expect "old radio" quality.
Encode with e.g. `ffmpeg -i in.wav -ac 1 -ar 16000 -b:a 16k out.mp3`.

## How it works

* `mp3/` is a standalone player ROM (`make mp3player.gba`) using libmad (`third_party/libmad`, GPL-2+, compatible with this project's GPLv3).
  Hot decoder code runs from IWRAM (about 19.7 KB), data from EWRAM, constant tables from ROM.
  Code in EWRAM would need 143% CPU, so IWRAM is a must.
* The firmware treats it like an emulator: a `.mp3` file is loaded after the player ROM, behind an `AMP3` header (see `mp3_header` in `src/emu.c`).
  The player is bundled as asset `MP3P` or can be placed at `/.superfw/emulators/mp3player.gba`.
* Controls: A pause, B exit to the firmware, Up/Down volume, L/R seek by 5%.
  The screen shows time, CPU load and buffer underruns.

## Testing

`tests/mp3/mktest.py` builds a ROM (player + mp3) to run in mGBA, and `tests/mp3/audio.c` captures its audio and a screenshot with libmgba.

## Status

Verified in mGBA only (correct pitch and timing, 0 underruns on a 16 kHz/16 kbps file). **Not tested on real hardware**, including real SD read speed and the exit path.
