#ifndef MEMALLOC_SLAB_POOL_H
#define MEMALLOC_SLAB_POOL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MemallocSlabRegistry MemallocSlabRegistry;

/* Slabs are mapped at an address aligned to their own size, so that the
 * owning slab of any slot pointer can be found by masking off the low
 * log2(kSlabSize) bits. Must be a power of two. */
#define MEMALLOC_SLAB_SIZE ((size_t)1u << 16)  /* 64 KiB */

/* Header stored at the start of every slab (i.e. at the slab's aligned
 * base address). Exposed so the allocator facade can identify which
 * pool owns a slot purely from its address. */
typedef struct MemallocSlabHeader {
    struct MemallocSlabHeader* next_partial;  /* next slab in this pool with a free slot */
    struct MemallocSlabHeader* next_all;      /* next slab in this pool, for teardown */
    void* free_list;                          /* embedded free list of free slots */
    uint32_t free_count;
    uint32_t slot_size;
} MemallocSlabHeader;

/* A pool of fixed-size slots carved out of mmap'd "slabs". Each slab embeds
 * its own free list directly in the unused slots (see README "Slab
 * Allocator Design"), so allocation/deallocation are O(1) pointer
 * pop/push operations once the pool's mutex is held. */
typedef struct MemallocSlabPool {
    pthread_mutex_t mutex;
    MemallocSlabRegistry* registry;
    MemallocSlabHeader* partial;  /* slabs with at least one free slot */
    MemallocSlabHeader* all;      /* every slab ever mapped, for teardown */
    size_t slot_size;
    size_t slots_per_slab;
    size_t first_slot_offset;
} MemallocSlabPool;

/* `registry` is notified of every slab base address mapped by this pool
 * so the allocator facade can route deallocate(p) correctly. */
void memalloc_slabpool_init(MemallocSlabPool* pool, size_t slot_size, MemallocSlabRegistry* registry);
void memalloc_slabpool_destroy(MemallocSlabPool* pool);

void* memalloc_slabpool_allocate(MemallocSlabPool* pool);
void memalloc_slabpool_deallocate(MemallocSlabPool* pool, void* p);

/* Batch variants used by ThreadCache to amortize lock acquisition.
 * allocate_batch pops up to `n` slots under a single lock, threads them
 * via their embedded next-pointer field, and returns the list head (or
 * NULL if OOM even after growing). *out_count is set to the actual
 * number obtained. */
void* memalloc_slabpool_allocate_batch(MemallocSlabPool* pool, uint32_t n, uint32_t* out_count);

/* deallocate_batch pushes all `count` slots in `list_head` (linked via
 * embedded next-pointer field) back to their owning slabs under one lock. */
void memalloc_slabpool_deallocate_batch(MemallocSlabPool* pool, void* list_head, uint32_t count);

/* Returns the number of slabs currently mapped by this pool. Test helper. */
size_t memalloc_slabpool_mapped_slab_count(MemallocSlabPool* pool);

static inline MemallocSlabHeader* memalloc_slabpool_header_for(void* p) {
    uintptr_t addr = (uintptr_t)p;
    return (MemallocSlabHeader*)(addr & ~(MEMALLOC_SLAB_SIZE - 1));
}

#endif  /* MEMALLOC_SLAB_POOL_H */
