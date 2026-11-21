/* a7_heap.c -- the game's malloc/free with guard bytes.
 *
 * Hardware 2026-11-17: loading a race crashed inside newlib's malloc with
 * r0 = 0x6d6c6170 ("palm" in ASCII): a string ran past the end of a heap
 * block and over the next chunk's header. bionic's dlmalloc (Android 2.3)
 * left slack after small blocks that the game may have relied on without
 * knowing; newlib packs them tighter.
 *
 * Every block the game gets (malloc, calloc, realloc, strdup, operator new)
 * is laid out as
 *
 *   [ head: magic, size, caller, magic2 ][ the game's bytes ][ tail: 0xA7 bytes ]
 *
 * so a small overrun lands in the tail instead of the allocator's header:
 * the crash should be gone, and when the block is freed the overrun is
 * logged once per caller with the block's size, the libAndroid.so
 * address that allocated it and the bytes written past its end.
 * A pointer without the head magic (memory the runtime allocated and the
 * game frees) goes to the real free/realloc. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#include "a7.h"

#define HEAD_MAGIC  0xA7A7F00Du
#define HEAD_MAGIC2 0x0DF0A7A7u
#define DEAD_MAGIC  0xDEADA7A7u
#define TAIL_BYTES  128 /* hardware: 8-byte overruns seen; >16 broke the next block */
#define TAIL_FILL   0xA7

typedef struct {
  uint32_t magic, size, caller, magic2;
} Head; /* 16 bytes: the game's pointer stays 8-aligned */

/* What the game holds right now (its own blocks, without our 32 bytes). */
static volatile uint32_t g_live_blocks;
static volatile uint64_t g_live_bytes, g_peak_bytes;

static uint32_t lib_off(uintptr_t a) {
  uintptr_t b = (uintptr_t)g_a7_mod.load_virtbase;
  return (b && a >= b) ? (uint32_t)(a - b) : (uint32_t)a;
}

void *a7_heap_alloc(size_t n, uintptr_t caller) {
  if (n > 0x7ff00000u)
    return NULL;
  Head *h = malloc(sizeof(Head) + n + TAIL_BYTES);
  if (!h) {
    static int told;
    if (told++ < 8) {
      struct mallinfo mi = mallinfo();
      debugPrintf("[heap] OUT OF MEMORY: %u bytes for libguest+0x%x; the game holds %u MB in %u blocks "
                  "(peak %u MB); heap in use %u MB, free in the arena %u MB\n",
                  (unsigned)n, (unsigned)lib_off(caller), (unsigned)(g_live_bytes >> 20), (unsigned)g_live_blocks,
                  (unsigned)(g_peak_bytes >> 20), (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20));
      log_flush_ring();
    }
    return NULL;
  }
  __atomic_add_fetch(&g_live_blocks, 1, __ATOMIC_RELAXED);
  uint64_t lb = __atomic_add_fetch(&g_live_bytes, (uint64_t)n, __ATOMIC_RELAXED);
  if (lb > g_peak_bytes)
    g_peak_bytes = lb;
  h->magic = HEAD_MAGIC;
  h->size = (uint32_t)n;
  h->caller = (uint32_t)caller;
  h->magic2 = HEAD_MAGIC2;
  memset((uint8_t *)(h + 1) + n, TAIL_FILL, TAIL_BYTES);
  return h + 1;
}

static int is_ours(const void *p) {
  const Head *h = (const Head *)p - 1;
  return ((uintptr_t)p & 7) == 0 && h->magic == HEAD_MAGIC && h->magic2 == HEAD_MAGIC2;
}

static void report(const Head *h, const char *what, uintptr_t freer) {
  static uint32_t seen[32];
  static int nseen, total;
  for (int i = 0; i < nseen; i++)
    if (seen[i] == h->caller)
      return;
  if (nseen < 32)
    seen[nseen++] = h->caller;
  if (++total > 32)
    return;
  const uint8_t *data = (const uint8_t *)(h + 1);
  const uint8_t *tail = data + h->size;
#define SHOW 32
  int reach = 0; /* how many bytes past the end were written */
  for (int i = 0; i < TAIL_BYTES; i++)
    if (tail[i] != TAIL_FILL)
      reach = i + 1;
  char hex[3 * SHOW + 1], txt[SHOW + 1], start[25];
  for (int i = 0; i < SHOW; i++) {
    snprintf(hex + 3 * i, 4, "%02x ", tail[i]);
    txt[i] = (tail[i] >= 32 && tail[i] < 127) ? (char)tail[i] : '.';
  }
  txt[SHOW] = 0;
  int ns = h->size < 24 ? (int)h->size : 24;
  for (int i = 0; i < ns; i++)
    start[i] = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
  start[ns] = 0;
  debugPrintf("[heap] %s: block of %u bytes from libguest+0x%x (freed from +0x%x), %d bytes written past "
              "its end%s\n[heap]   starts \"%s\", past its end: %s\"%s\"\n",
              what, (unsigned)h->size, (unsigned)lib_off(h->caller), (unsigned)lib_off(freer), reach,
              reach >= TAIL_BYTES ? " (or more: the guard is full)" : "", start, hex, txt);
  log_flush_ring();
}

void a7_heap_free(void *p, uintptr_t caller) {
  if (!p)
    return;
  if (!is_ours(p)) {
    Head *h = (Head *)p - 1;
    if (((uintptr_t)p & 7) == 0 && h->magic == DEAD_MAGIC) {
      static int n;
      if (n++ < 8)
        debugPrintf("[heap] double free of %p (from libguest+0x%x): ignored\n", p, (unsigned)lib_off(caller));
      return;
    }
    free(p);
    return;
  }
  Head *h = (Head *)p - 1;
  const uint8_t *tail = (const uint8_t *)p + h->size;
  for (int i = 0; i < TAIL_BYTES; i++)
    if (tail[i] != TAIL_FILL) {
      report(h, "OVERRUN", caller);
      break;
    }
  h->magic = DEAD_MAGIC;
  __atomic_sub_fetch(&g_live_blocks, 1, __ATOMIC_RELAXED);
  __atomic_sub_fetch(&g_live_bytes, (uint64_t)h->size, __ATOMIC_RELAXED);
  free(h);
}

static void *heap_realloc(void *p, size_t n, uintptr_t caller) {
  if (!p)
    return a7_heap_alloc(n, caller);
  if (!is_ours(p))
    return realloc(p, n);
  if (!n) {
    a7_heap_free(p, caller);
    return NULL;
  }
  Head *h = (Head *)p - 1;
  void *q = a7_heap_alloc(n, h->caller);
  if (!q)
    return NULL;
  memcpy(q, p, h->size < n ? h->size : n);
  a7_heap_free(p, caller);
  return q;
}

#define CALLER ((uintptr_t)__builtin_return_address(0))

void *b_malloc(size_t n) { return a7_heap_alloc(n, CALLER); }
void b_free(void *p) { a7_heap_free(p, CALLER); }
void *b_realloc(void *p, size_t n) { return heap_realloc(p, n, CALLER); }
void *b_calloc(size_t a, size_t b) {
  if (b && a > 0x7ff00000u / b)
    return NULL;
  void *p = a7_heap_alloc(a * b, CALLER);
  if (p)
    memset(p, 0, a * b);
  return p;
}
char *b_strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = a7_heap_alloc(n, CALLER);
  if (p)
    memcpy(p, s, n);
  return p;
}

/* Every 300 frames from the frame loop: the game's share against the whole
 * heap. The game's MB growing with each car loaded: the game keeps them. The
 * heap growing while the game's MB does not: the runtime, Mesa or mmap. */
void a7_heap_report(const char *why) {
  struct mallinfo mi = mallinfo();
  debugPrintf("[mem] %s: game %u MB in %u blocks (peak %u MB); heap in use %u MB, free in arena %u MB\n",
              why, (unsigned)(g_live_bytes >> 20), (unsigned)g_live_blocks, (unsigned)(g_peak_bytes >> 20),
              (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20));
}
