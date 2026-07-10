#include "memalloc/allocator.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "memalloc/common.h"
#include "memalloc/free_list.h"
#include "memalloc/slab_pool.h"
#include "memalloc/thread_cache.h"
#include "slab_registry.h"

/* Top-level facade state: dispatches allocate/deallocate/reallocate between
 * the per-thread cache -> slab pools (small, fixed-size requests) and the
 * boundary-tag free list (everything above MEMALLOC_SLAB_THRESHOLD). See
 * README. A single zero-initialized static instance is used, guarded by
 * pthread_once -- since C has no destructors, there is no destruction-order
 * hazard between this singleton and thread-exit ThreadCache flushes the way
 * the original C++ version had to work around with a leaked heap object. */
typedef struct {
    MemallocSlabRegistry registry;
    MemallocSlabPool pools[MEMALLOC_NUM_SLAB_SIZE_CLASSES];
    MemallocFreeList free_list;
} MemallocAllocatorState;

static MemallocAllocatorState g_allocator;
static pthread_once_t g_allocator_once = PTHREAD_ONCE_INIT;

static void memalloc_allocator_init(void) {
    memalloc_slabregistry_init(&g_allocator.registry);
    for (size_t i = 0; i < MEMALLOC_NUM_SLAB_SIZE_CLASSES; ++i) {
        memalloc_slabpool_init(&g_allocator.pools[i], memalloc_slab_size_classes[i], &g_allocator.registry);
    }
    memalloc_freelist_init(&g_allocator.free_list);
}

static MemallocAllocatorState* memalloc_allocator_instance(void) {
    pthread_once(&g_allocator_once, memalloc_allocator_init);
    return &g_allocator;
}

/* size_class is one of memalloc_slab_size_classes (a power of two from 8 to
 * 512); maps it to the corresponding index into pools[]. */
static size_t memalloc_pool_index(size_t size_class) {
    return (size_t)__builtin_ctz((unsigned)size_class) - 3;
}

/* Returns the slab header owning `p`, or NULL if `p` was not allocated from
 * a slab pool (i.e. it belongs to the free list). */
static MemallocSlabHeader* memalloc_slab_owner(MemallocAllocatorState* a, void* p) {
    uintptr_t base = (uintptr_t)p & ~(MEMALLOC_SLAB_SIZE - 1);
    if (!memalloc_slabregistry_contains(&a->registry, base)) return NULL;
    return (MemallocSlabHeader*)base;
}

void* memalloc_allocate(size_t size) {
    MemallocAllocatorState* a = memalloc_allocator_instance();
    size_t cls = memalloc_slab_class_for(size);
    if (cls != 0) {
        MemallocThreadCache* tc = memalloc_thread_cache_for(a->pools);
        return memalloc_threadcache_allocate(tc, memalloc_pool_index(cls));
    }
    return memalloc_freelist_allocate(&a->free_list, size);
}

void memalloc_deallocate(void* p) {
    if (!p) return;
    MemallocAllocatorState* a = memalloc_allocator_instance();
    MemallocSlabHeader* slab = memalloc_slab_owner(a, p);
    if (slab) {
        MemallocThreadCache* tc = memalloc_thread_cache_for(a->pools);
        memalloc_threadcache_deallocate(tc, memalloc_pool_index(slab->slot_size), p);
    } else {
        memalloc_freelist_deallocate(&a->free_list, p);
    }
}

void* memalloc_reallocate(void* p, size_t new_size) {
    MemallocAllocatorState* a = memalloc_allocator_instance();
    if (!p) return memalloc_allocate(new_size);
    if (new_size == 0) {
        memalloc_deallocate(p);
        return NULL;
    }

    MemallocSlabHeader* slab = memalloc_slab_owner(a, p);
    if (slab) {
        size_t old_size = slab->slot_size;
        if (new_size <= old_size) return p;
        void* np = memalloc_allocate(new_size);
        if (!np) return NULL;
        memcpy(np, p, old_size);
        MemallocThreadCache* tc = memalloc_thread_cache_for(a->pools);
        memalloc_threadcache_deallocate(tc, memalloc_pool_index(old_size), p);
        return np;
    }
    return memalloc_freelist_reallocate(&a->free_list, p, new_size);
}

size_t memalloc_usable_size(void* p) {
    if (!p) return 0;
    MemallocAllocatorState* a = memalloc_allocator_instance();
    MemallocSlabHeader* slab = memalloc_slab_owner(a, p);
    if (slab) return slab->slot_size;
    return memalloc_freelist_usable_size(p);
}
