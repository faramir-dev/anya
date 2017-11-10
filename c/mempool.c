#include "mempool.h"

#include <string.h>

#define CPU_PAGE_SIZE 4096
#define MP_CHUNK_TAIL ALIGN_TO(sizeof(struct mempool_chunk), __BIGGEST_ALIGNMENT__)
#define MP_SIZE_MAX (~0U - MP_CHUNK_TAIL - CPU_PAGE_SIZE)

struct mempool_chunk {
  struct mempool_chunk *next;
  size_t size;
};

static size_t
mp_align_size(size_t size) {
  return ALIGN_TO(size, __BIGGEST_ALIGNMENT__);
}

void
mp_init(struct mempool *pool, size_t chunk_size) {
  chunk_size = mp_align_size(MAX(sizeof(struct mempool), chunk_size));
  *pool = (struct mempool) {
    .chunk_size = chunk_size,
    .threshold = chunk_size >> 1,
    .last_big = &pool->last_big
  };
}

static void *
mp_new_big_chunk(size_t size) {
  struct mempool_chunk *chunk;
  chunk = XMALLOC(size + MP_CHUNK_TAIL) + size;
  chunk->size = size;
  return chunk;
}

static void
mp_free_big_chunk(struct mempool_chunk *chunk) {
  XFREE((void *)chunk - chunk->size);
}

static void *
mp_new_chunk(size_t size) {
  return mp_new_big_chunk(size);
}

static void
mp_free_chunk(struct mempool_chunk *chunk) {
  mp_free_big_chunk(chunk);
}

struct mempool *
mp_new(size_t chunk_size) {
  chunk_size = mp_align_size(MAX(sizeof(struct mempool), chunk_size));
  struct mempool_chunk *chunk = mp_new_chunk(chunk_size);
  struct mempool *pool = (void *)chunk - chunk_size;
  chunk->next = NULL;
  *pool = (struct mempool) {
    .state = { .free = { chunk_size - sizeof(*pool) }, .last = { chunk } },
    .chunk_size = chunk_size,
    .threshold = chunk_size >> 1,
    .last_big = &pool->last_big };
  return pool;
}

static void
mp_free_chain(struct mempool_chunk *chunk) {
  while (chunk) {
    struct mempool_chunk *next = chunk->next;
    mp_free_chunk(chunk);
    chunk = next;
  }
}

static void
mp_free_big_chain(struct mempool_chunk *chunk) {
  while (chunk) {
    struct mempool_chunk *next = chunk->next;
    mp_free_big_chunk(chunk);
    chunk = next;
  }
}

void
mp_delete(struct mempool *pool) {
  mp_free_big_chain(pool->state.last[1]);
  mp_free_chain(pool->unused);
  mp_free_chain(pool->state.last[0]); // can contain the mempool structure
}

void
mp_flush(struct mempool *pool) {
  mp_free_big_chain(pool->state.last[1]);
  struct mempool_chunk *chunk, *next;
  for (chunk = pool->state.last[0]; chunk && (void *)chunk - chunk->size != pool; chunk = next) {
    next = chunk->next;
    chunk->next = pool->unused;
    pool->unused = chunk;
  }
  pool->state.last[0] = chunk;
  pool->state.free[0] = chunk ? chunk->size - sizeof(*pool) : 0;
  pool->state.last[1] = NULL;
  pool->state.free[1] = 0;
  pool->state.next = NULL;
  pool->last_big = &pool->last_big;
}

static void
mp_stats_chain(struct mempool_chunk *chunk, struct mempool_stats *stats, size_t idx) {
  while (chunk) {
    stats->chain_size[idx] += chunk->size + sizeof(*chunk);
    stats->chain_count[idx]++;
    chunk = chunk->next;
  }
  stats->total_size += stats->chain_size[idx];
}

void
mp_stats(struct mempool *pool, struct mempool_stats *stats) {
  bzero(stats, sizeof(*stats));
  mp_stats_chain(pool->state.last[0], stats, 0);
  mp_stats_chain(pool->state.last[1], stats, 1);
  mp_stats_chain(pool->unused, stats, 2);
}

void *
mp_alloc_internal(struct mempool *pool, size_t size) {
  struct mempool_chunk *chunk;
  if (size <= pool->threshold) {
    pool->idx = 0;
    if (pool->unused) {
       chunk = pool->unused;
       pool->unused = chunk->next;
    } else
      chunk = mp_new_chunk(pool->chunk_size);
    chunk->next = pool->state.last[0];
    pool->state.last[0] = chunk;
    pool->state.free[0] = pool->chunk_size - size;
    return (void *)chunk - pool->chunk_size;
  } else if (likely(size <= MP_SIZE_MAX)) {
    pool->idx = 1;
    size_t aligned = ALIGN_TO(size, __BIGGEST_ALIGNMENT__);
    chunk = mp_new_big_chunk(aligned);
    chunk->next = pool->state.last[1];
    pool->state.last[1] = chunk;
    pool->state.free[1] = aligned - size;
    return pool->last_big = (void *)chunk - aligned;
  } else
    FATAL(255, "Cannot allocate %zu bytes from a mempool", size);
}

void *
mp_alloc(struct mempool *pool, size_t size) {
  return mp_alloc_fast(pool, size);
}

void *
mp_alloc_noalign(struct mempool *pool, size_t size) {
  return mp_alloc_fast_noalign(pool, size);
}

void *
mp_alloc_zero(struct mempool *pool, size_t size) {
  void *ptr = mp_alloc_fast(pool, size);
  bzero(ptr, size);
  return ptr;
}

void *
mp_start_internal(struct mempool *pool, size_t size) {
  void *ptr = mp_alloc_internal(pool, size);
  pool->state.free[pool->idx] += size;
  return ptr;
}

void *
mp_start(struct mempool *pool, size_t size) {
  return mp_start_fast(pool, size);
}

void *
mp_start_noalign(struct mempool *pool, size_t size) {
  return mp_start_fast_noalign(pool, size);
}

void *
mp_grow_internal(struct mempool *pool, size_t size) {
  if (unlikely(size > MP_SIZE_MAX))
    FATAL(255, "Cannot allocate %zu bytes of memory", size);
  size_t avail = mp_avail(pool);
  void *ptr = mp_ptr(pool);
  if (pool->idx) {
    size_t amortized = likely(avail <= MP_SIZE_MAX / 2) ? avail * 2 : MP_SIZE_MAX;
    amortized = MAX(amortized, size);
    amortized = ALIGN_TO(amortized, __BIGGEST_ALIGNMENT__);
    struct mempool_chunk *chunk = pool->state.last[1], *next = chunk->next;
    ptr = XREALLOC(ptr, amortized + MP_CHUNK_TAIL);
    chunk = ptr + amortized;
    chunk->next = next;
    chunk->size = amortized;
    pool->state.last[1] = chunk;
    pool->state.free[1] = amortized;
    pool->last_big = ptr;
    return ptr;
  } else {
    void *p = mp_start_internal(pool, size);
    memcpy(p, ptr, avail);
    return p;
  }
}

size_t
mp_open(struct mempool *pool, void *ptr) {
  return mp_open_fast(pool, ptr);
}

void *
mp_realloc(struct mempool *pool, void *ptr, size_t size) {
  return mp_realloc_fast(pool, ptr, size);
}

void *
mp_realloc_zero(struct mempool *pool, void *ptr, size_t size) {
  size_t old_size = mp_open_fast(pool, ptr);
  ptr = mp_grow(pool, size);
  if (size > old_size)
    bzero(ptr + old_size, size - old_size);
  mp_end(pool, ptr + size);
  return ptr;
}

void *
mp_spread_internal(struct mempool *pool, void *p, size_t size) {
  void *old = mp_ptr(pool);
  void *new = mp_grow_internal(pool, p-old+size);
  return p-old+new;
}

void
mp_restore(struct mempool *pool, struct mempool_state *state) {
  struct mempool_chunk *chunk, *next;
  struct mempool_state s = *state;
  for (chunk = pool->state.last[0]; chunk != s.last[0]; chunk = next) {
    next = chunk->next;
    chunk->next = pool->unused;
    pool->unused = chunk;
  }
  for (chunk = pool->state.last[1]; chunk != s.last[1]; chunk = next) {
    next = chunk->next;
    mp_free_big_chunk(chunk);
  }
  pool->state = s;
  pool->last_big = &pool->last_big;
}

struct mempool_state *
mp_push(struct mempool *pool) {
  struct mempool_state state = pool->state;
  struct mempool_state *p = mp_alloc_fast(pool, sizeof(*p));
  *p = state;
  pool->state.next = p;
  return p;
}

void
mp_pop(struct mempool *pool) {
  assert(pool->state.next);
  struct mempool_state state = pool->state;
  mp_restore(pool, &state);
}

char *
mp_strdup(struct mempool *p, char *s) {
  size_t l = strlen(s) + 1;
  char *t = mp_alloc_fast_noalign(p, l);
  memcpy(t, s, l);
  return t;
}

void *
mp_memdup(struct mempool *p, void *s, size_t len) {
  void *t = mp_alloc_fast(p, len);
  memcpy(t, s, len);
  return t;
}

char *
mp_multicat(struct mempool *p, ...) {
  va_list args, a;
  va_start(args, p);
  char *x, *y;
  size_t cnt = 0;
  va_copy(a, args);
  while ((x = va_arg(a, char *)))
    cnt++;
  size_t *sizes = alloca(cnt * sizeof(size_t));
  size_t len = 1;
  cnt = 0;
  va_end(a);
  va_copy(a, args);
  while ((x = va_arg(a, char *)))
    len += sizes[cnt++] = strlen(x);
  char *buf = mp_alloc_fast_noalign(p, len);
  y = buf;
  va_end(a);
  cnt = 0;
  while ((x = va_arg(args, char *))) {
    memcpy(y, x, sizes[cnt]);
    y += sizes[cnt++];
  }
  *y = 0;
  va_end(args);
  return buf;
}

char *
mp_strjoin(struct mempool *p, char **a, size_t n, size_t sep) {
  size_t sizes[n];
  size_t len = 1;
  for (size_t i=0; i<n; i++)
    len += sizes[i] = strlen(a[i]);
  if (sep && n)
    len += n-1;
  char *dest = mp_alloc_fast_noalign(p, len);
  char *d = dest;
  for (size_t i=0; i<n; i++) {
      if (sep && i)
	*d++ = sep;
      memcpy(d, a[i], sizes[i]);
      d += sizes[i];
  }
  *d = 0;
  return dest;
}

static char *
mp_vprintf_at(struct mempool *mp, size_t ofs, const char *fmt, va_list args) {
  char *ret = mp_grow(mp, ofs + 1) + ofs;
  va_list args2;
  va_copy(args2, args);
  int cnt = vsnprintf(ret, mp_avail(mp) - ofs, fmt, args2);
  va_end(args2);
  if ((size_t)cnt >= mp_avail(mp) - ofs) {
      ret = mp_grow(mp, cnt + 1) + ofs;
      va_copy(args2, args);
      int cnt2 = vsnprintf(ret, cnt + 1, fmt, args2);
      va_end(args2);
      assert(cnt2 == cnt);
  }
  mp_end(mp, ret + cnt + 1);
  return ret - ofs;
}

char *
mp_vprintf(struct mempool *mp, const char *fmt, va_list args) {
  mp_start(mp, 1);
  return mp_vprintf_at(mp, 0, fmt, args);
}

char *
mp_printf(struct mempool *p, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char *res = mp_vprintf(p, fmt, args);
  va_end(args);
  return res;
}

char *
mp_vprintf_append(struct mempool *mp, char *ptr, const char *fmt, va_list args) {
  size_t ofs = mp_open(mp, ptr);
  assert(ofs);
  return mp_vprintf_at(mp, ofs - 1, fmt, args);
}

char *
mp_printf_append(struct mempool *mp, char *ptr, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char *res = mp_vprintf_append(mp, ptr, fmt, args);
  va_end(args);
  return res;
}
