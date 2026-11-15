// The little bit of libc that libmad needs, and the Layer I/II decoders it doesn't have.
#include <stddef.h>
#include "mad.h"

void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n) {
  char *a = d; const char *b = s;
  if (a < b) while (n--) *a++ = *b++;
  else { a += n; b += n; while (n--) *--a = *--b; }
  return d;
}
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = c; return d; }
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
  return 0;
}

// libmad allocates the Layer III buffers once (about 7.5 KiB).
static char heap[8 * 1024] __attribute__((aligned(4)));
static size_t heap_used;
void *malloc(size_t n) {
  n = (n + 3) & ~3U;
  if (heap_used + n > sizeof(heap)) return 0;
  void *r = &heap[heap_used];
  heap_used += n;
  return r;
}
void *calloc(size_t a, size_t b) { char *p = malloc(a * b); for (size_t i = 0; p && i < a * b; i++) p[i] = 0; return p; }
void free(void *p) { (void)p; }

// Only Layer III is supported.
int mad_layer_I(struct mad_stream *stream, struct mad_frame *frame) { stream->error = MAD_ERROR_BADLAYER; return -1; }
int mad_layer_II(struct mad_stream *stream, struct mad_frame *frame) { stream->error = MAD_ERROR_BADLAYER; return -1; }
