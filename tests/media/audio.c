// Runs a GBA ROM in libmgba for N frames and writes the audio (left channel, 32768 Hz, s16) and a screenshot.
// Build: gcc -O1 -o audio audio.c -lmgba     Usage: audio rom.gba frames out.raw out.ppm
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/blip_buf.h>
#include <mgba-util/vfs.h>
#include <mgba/gba/core.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  struct mCore *core = mCoreFind(argv[1]);
  core->init(core); mCoreInitConfig(core, NULL);
  unsigned w, h; core->desiredVideoDimensions(core, &w, &h);
  color_t *buf = malloc(w * h * 4); core->setVideoBuffer(core, buf, w);
  core->setAudioBufferSize(core, 2048);
  struct VFile *vf = VFileOpen(argv[1], O_RDONLY);
  if (!vf || !core->loadROM(core, vf)) { puts("load failed"); return 1; }
  blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), 32768);
  blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), 32768);
  core->reset(core);
  FILE *out = fopen(argv[3], "wb");
  int frames = atoi(argv[2]);
  int16_t tmp[4096];
  for (int i = 0; i < frames; i++) {
    core->runFrame(core);
    blip_t *l = core->getAudioChannel(core, 0);
    int n = blip_samples_avail(l);
    while (n > 0) { int c = n > 2048 ? 2048 : n; c = blip_read_samples(l, tmp, c, 0); fwrite(tmp, 2, c, out); n -= c; }
    blip_t *r = core->getAudioChannel(core, 1);
    blip_clear(r);
  }
  fclose(out);
  FILE *f = fopen(argv[4], "wb"); fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (unsigned i = 0; i < w * h; i++) { unsigned c = buf[i]; fputc(c & 0xFF, f); fputc((c >> 8) & 0xFF, f); fputc((c >> 16) & 0xFF, f); }
  fclose(f);
  core->deinit(core);
  return 0;
}
