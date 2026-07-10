#ifndef MEMALLOC_FREE_LIST_H
#define MEMALLOC_FREE_LIST_H

#include <pthread.h>
#include <stddef.h>

/* Opaque to callers -- full layout lives in src/free_list.c. Only pointers
 * to these are ever stored outside that translation unit. */
typedef struct MemallocFLHeader MemallocFLHeader;
typedef struct MemallocFLArenaHeader MemallocFLArenaHeader;

/* Variable-size allocator backed by mmap'd arenas, using Knuth's boundary
 * tags for O(1) coalescing on free (see README "Variable-Size Allocator").
 *
 * Block layout (sizes are always multiples of 16):
 *
 *   [Header: size_t size|flag] [ ... payload ... ] [Footer: size_t size|flag]
 *
 * Free blocks additionally store {prev, next} pointers (a free-node) at the
 * start of their payload, threading an explicit doubly-linked free list used
 * for first-fit search. */
typedef struct {
    pthread_mutex_t mutex;
    MemallocFLHeader* free_list;
    MemallocFLArenaHeader* arenas;
} MemallocFreeList;

void memalloc_freelist_init(MemallocFreeList* fl);
void memalloc_freelist_destroy(MemallocFreeList* fl);

void* memalloc_freelist_allocate(MemallocFreeList* fl, size_t size);
void memalloc_freelist_deallocate(MemallocFreeList* fl, void* p);
void* memalloc_freelist_reallocate(MemallocFreeList* fl, void* p, size_t new_size);

/* Usable payload size of an allocation (>= the size it was requested with). */
size_t memalloc_freelist_usable_size(void* p);

/* Total block size (header + payload + footer). Test/debug helper. */
size_t memalloc_freelist_block_total_size(void* p);

/* Number of blocks currently on the free list. Test/debug helper. */
size_t memalloc_freelist_free_block_count(MemallocFreeList* fl);

#endif  /* MEMALLOC_FREE_LIST_H */
