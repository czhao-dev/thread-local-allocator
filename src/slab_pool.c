#include "memalloc/slab_pool.h"

#include <stdbool.h>

#include "mmap_utils.h"
#include "slab_registry.h"

#include "memalloc/common.h"

void memalloc_slabpool_init(MemallocSlabPool* pool, size_t slot_size, MemallocSlabRegistry* registry) {
    pthread_mutex_init(&pool->mutex, NULL);
    pool->registry = registry;
    pool->partial = NULL;
    pool->all = NULL;
    pool->slot_size = slot_size;
    pool->first_slot_offset = memalloc_align_up(sizeof(MemallocSlabHeader), slot_size);
    pool->slots_per_slab = (MEMALLOC_SLAB_SIZE - pool->first_slot_offset) / slot_size;
}

void memalloc_slabpool_destroy(MemallocSlabPool* pool) {
    for (MemallocSlabHeader* slab = pool->all; slab != NULL;) {
        MemallocSlabHeader* next = slab->next_all;
        memalloc_munmap_region(slab, MEMALLOC_SLAB_SIZE);
        slab = next;
    }
    pthread_mutex_destroy(&pool->mutex);
}

static bool memalloc_slabpool_grow(MemallocSlabPool* pool) {
    void* mem = memalloc_mmap_aligned(MEMALLOC_SLAB_SIZE, MEMALLOC_SLAB_SIZE);
    if (!mem) return false;

    MemallocSlabHeader* slab = (MemallocSlabHeader*)mem;

    slab->slot_size = (uint32_t)pool->slot_size;
    slab->free_count = (uint32_t)pool->slots_per_slab;

    /* Build the embedded free list, threading a `void*` "next" pointer
     * through each unused slot. */
    uintptr_t base = (uintptr_t)mem;
    for (size_t i = 0; i < pool->slots_per_slab; ++i) {
        void* slot = (void*)(base + pool->first_slot_offset + i * pool->slot_size);
        void* next = (i + 1 < pool->slots_per_slab)
                         ? (void*)(base + pool->first_slot_offset + (i + 1) * pool->slot_size)
                         : NULL;
        *(void**)slot = next;
    }
    slab->free_list = (void*)(base + pool->first_slot_offset);

    slab->next_partial = pool->partial;
    pool->partial = slab;
    slab->next_all = pool->all;
    pool->all = slab;

    memalloc_slabregistry_insert(pool->registry, base);
    return true;
}

void* memalloc_slabpool_allocate(MemallocSlabPool* pool) {
    pthread_mutex_lock(&pool->mutex);
    if (!pool->partial && !memalloc_slabpool_grow(pool)) {
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }

    MemallocSlabHeader* slab = pool->partial;
    void* p = slab->free_list;
    slab->free_list = *(void**)p;
    --slab->free_count;

    if (slab->free_count == 0) {
        pool->partial = slab->next_partial;
        slab->next_partial = NULL;
    }
    pthread_mutex_unlock(&pool->mutex);
    return p;
}

void memalloc_slabpool_deallocate(MemallocSlabPool* pool, void* p) {
    pthread_mutex_lock(&pool->mutex);
    MemallocSlabHeader* slab = memalloc_slabpool_header_for(p);

    bool was_full = (slab->free_count == 0);
    *(void**)p = slab->free_list;
    slab->free_list = p;
    ++slab->free_count;

    if (was_full) {
        slab->next_partial = pool->partial;
        pool->partial = slab;
    }
    pthread_mutex_unlock(&pool->mutex);
}

void* memalloc_slabpool_allocate_batch(MemallocSlabPool* pool, uint32_t n, uint32_t* out_count) {
    pthread_mutex_lock(&pool->mutex);
    void* list_head = NULL;
    uint32_t count = 0;
    while (count < n) {
        if (!pool->partial && !memalloc_slabpool_grow(pool)) break;
        MemallocSlabHeader* slab = pool->partial;
        void* p = slab->free_list;
        slab->free_list = *(void**)p;
        --slab->free_count;
        if (slab->free_count == 0) {
            pool->partial = slab->next_partial;
            slab->next_partial = NULL;
        }
        *(void**)p = list_head;
        list_head = p;
        ++count;
    }
    *out_count = count;
    pthread_mutex_unlock(&pool->mutex);
    return list_head;
}

void memalloc_slabpool_deallocate_batch(MemallocSlabPool* pool, void* list_head, uint32_t count) {
    if (!list_head) return;
    (void)count;
    pthread_mutex_lock(&pool->mutex);
    for (void* p = list_head; p;) {
        void* next = *(void**)p;
        MemallocSlabHeader* slab = memalloc_slabpool_header_for(p);
        bool was_full = (slab->free_count == 0);
        *(void**)p = slab->free_list;
        slab->free_list = p;
        ++slab->free_count;
        if (was_full) {
            slab->next_partial = pool->partial;
            pool->partial = slab;
        }
        p = next;
    }
    pthread_mutex_unlock(&pool->mutex);
}

size_t memalloc_slabpool_mapped_slab_count(MemallocSlabPool* pool) {
    pthread_mutex_lock(&pool->mutex);
    size_t n = 0;
    for (MemallocSlabHeader* s = pool->all; s; s = s->next_all) ++n;
    pthread_mutex_unlock(&pool->mutex);
    return n;
}
