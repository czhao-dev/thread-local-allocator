#ifndef MEMALLOC_THREAD_CACHE_H
#define MEMALLOC_THREAD_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memalloc/common.h"

typedef struct MemallocSlabPool MemallocSlabPool;

#define MEMALLOC_TC_REFILL_BATCH ((uint32_t)32)
#define MEMALLOC_TC_HIGH_WATER   ((uint32_t)64)

typedef struct {
    void* head;
    uint32_t count;
} MemallocTCBucket;

/* Per-thread front-end cache for the slab allocator. Each thread holds one
 * MemallocThreadCache with a small free list per size class (8..512B). On the
 * common path there is no locking -- allocation/deallocation are a pointer
 * pop/push.
 *
 * When a bucket empties, MEMALLOC_TC_REFILL_BATCH slots are pulled from the
 * central SlabPool in a single locked call. When a bucket exceeds
 * MEMALLOC_TC_HIGH_WATER, MEMALLOC_TC_REFILL_BATCH slots are flushed back in
 * a single locked call (hysteresis keeps the bucket near
 * MEMALLOC_TC_HIGH_WATER/2, avoiding flush/refill thrashing at the
 * boundary).
 *
 * Thread-exit: a pthread_key_t destructor (registered by
 * memalloc_thread_cache_for) flushes all buckets back to the central pools.
 * If malloc is called again on this thread after that destructor has run
 * (e.g. by another library's TLS destructor), the `dead` guard redirects all
 * operations directly to the central SlabPool, bypassing the stale cache. */
typedef struct {
    MemallocSlabPool* pools;
    MemallocTCBucket buckets[MEMALLOC_NUM_SLAB_SIZE_CLASSES];
    bool dead;  /* true after thread-exit flush; guards post-exit calls */
} MemallocThreadCache;

void memalloc_threadcache_init(MemallocThreadCache* tc, MemallocSlabPool* pools);

/* Returns a slot for pool index `idx`, or NULL on OOM. If called after
 * thread-exit flush, bypasses the cache and goes to the central pool. */
void* memalloc_threadcache_allocate(MemallocThreadCache* tc, size_t idx);

/* Returns `p` to the cache for pool index `idx`. Flushes a batch to the
 * central pool if the bucket exceeds MEMALLOC_TC_HIGH_WATER. If called after
 * thread-exit flush, goes directly to the central pool. */
void memalloc_threadcache_deallocate(MemallocThreadCache* tc, size_t idx, void* p);

/* Flushes all cached slots back to their central pools. Safe to call
 * multiple times (subsequent calls are a no-op). */
void memalloc_threadcache_flush_all(MemallocThreadCache* tc);

/* Returns the calling thread's ThreadCache, constructing it on first call.
 * `pools` must point to the Allocator's slab pool array and must outlive the
 * calling thread (guaranteed since the Allocator is a static-storage
 * singleton). */
MemallocThreadCache* memalloc_thread_cache_for(MemallocSlabPool* pools);

#endif  /* MEMALLOC_THREAD_CACHE_H */
