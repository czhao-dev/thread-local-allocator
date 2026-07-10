#include "memalloc/thread_cache.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memalloc/slab_pool.h"
#include "mmap_utils.h"

void memalloc_threadcache_init(MemallocThreadCache* tc, MemallocSlabPool* pools) {
    tc->pools = pools;
    memset(tc->buckets, 0, sizeof(tc->buckets));
    tc->dead = false;
}

void memalloc_threadcache_flush_all(MemallocThreadCache* tc) {
    for (size_t i = 0; i < MEMALLOC_NUM_SLAB_SIZE_CLASSES; ++i) {
        MemallocTCBucket* b = &tc->buckets[i];
        if (b->count > 0) {
            memalloc_slabpool_deallocate_batch(&tc->pools[i], b->head, b->count);
            b->head = NULL;
            b->count = 0;
        }
    }
}

void* memalloc_threadcache_allocate(MemallocThreadCache* tc, size_t idx) {
    if (tc->dead) return memalloc_slabpool_allocate(&tc->pools[idx]);

    MemallocTCBucket* b = &tc->buckets[idx];
    if (b->count == 0) {
        uint32_t got = 0;
        b->head = memalloc_slabpool_allocate_batch(&tc->pools[idx], MEMALLOC_TC_REFILL_BATCH, &got);
        b->count = got;
        if (b->count == 0) return NULL;  /* OOM */
    }
    void* p = b->head;
    b->head = *(void**)p;
    --b->count;
    return p;
}

void memalloc_threadcache_deallocate(MemallocThreadCache* tc, size_t idx, void* p) {
    if (tc->dead) {
        memalloc_slabpool_deallocate(&tc->pools[idx], p);
        return;
    }

    MemallocTCBucket* b = &tc->buckets[idx];
    *(void**)p = b->head;
    b->head = p;
    ++b->count;

    if (b->count > MEMALLOC_TC_HIGH_WATER) {
        /* Split off MEMALLOC_TC_REFILL_BATCH objects from the front of the
         * bucket to flush. */
        void* flush_head = b->head;
        void* cursor = b->head;
        for (uint32_t i = 1; i < MEMALLOC_TC_REFILL_BATCH; ++i) cursor = *(void**)cursor;
        b->head = *(void**)cursor;
        b->count -= MEMALLOC_TC_REFILL_BATCH;
        *(void**)cursor = NULL;  /* terminate the flush sublist */
        memalloc_slabpool_deallocate_batch(&tc->pools[idx], flush_head, MEMALLOC_TC_REFILL_BATCH);
    }
}

/* g_thread_cache_ptr is a _Thread_local *pointer* used only as a fast
 * per-thread cache of "where is my MemallocThreadCache" -- it is never
 * touched by the pthread_key destructor below, so it doesn't matter whether
 * the runtime's own TLS teardown reclaims it before or after that destructor
 * runs.
 *
 * The MemallocThreadCache struct itself is deliberately backed by an mmap'd
 * allocation rather than a _Thread_local object: on platforms where PIC code
 * forces the "dynamic" TLS model (e.g. Apple's dyld TLV implementation), a
 * plain _Thread_local object is heap-backed and freed by the runtime's own
 * per-thread cleanup, which can run before or interleaved with a
 * pthread_key destructor -- using its address as that destructor's argument
 * is then a use-after-free. mmap'ing it ourselves means we alone control its
 * lifetime. This also sidesteps recursing into the allocator we're
 * implementing (a plain malloc() here would call back into
 * memalloc_allocate()). */
static _Thread_local MemallocThreadCache* g_thread_cache_ptr = NULL;

static pthread_key_t g_thread_cache_key;
static pthread_once_t g_thread_cache_key_once = PTHREAD_ONCE_INIT;

static void memalloc_threadcache_key_destructor(void* arg) {
    MemallocThreadCache* tc = (MemallocThreadCache*)arg;
    memalloc_threadcache_flush_all(tc);
    tc->dead = true;
}

static void memalloc_threadcache_make_key(void) {
    pthread_key_create(&g_thread_cache_key, memalloc_threadcache_key_destructor);
}

MemallocThreadCache* memalloc_thread_cache_for(MemallocSlabPool* pools) {
    /* pools is used only on first construction per thread; subsequent calls
     * with the same (singleton) pools pointer are harmless. */
    if (!g_thread_cache_ptr) {
        pthread_once(&g_thread_cache_key_once, memalloc_threadcache_make_key);

        MemallocThreadCache* tc = (MemallocThreadCache*)memalloc_mmap_region(sizeof(MemallocThreadCache));
        if (!tc) {
            fprintf(stderr, "memalloc: failed to map per-thread cache (out of memory)\n");
            abort();
        }
        memalloc_threadcache_init(tc, pools);
        pthread_setspecific(g_thread_cache_key, tc);
        g_thread_cache_ptr = tc;
    }
    return g_thread_cache_ptr;
}
