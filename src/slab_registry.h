#ifndef MEMALLOC_SLAB_REGISTRY_H
#define MEMALLOC_SLAB_REGISTRY_H

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Records the base addresses of every slab that has been mapped, so that
 * `free(p)` can determine in O(1) whether `p` falls inside a slab (and
 * therefore should be returned to a SlabPool) or belongs to the free-list
 * heap. Backed by mmap'd memory directly -- it must never call malloc, since
 * it is on the deallocation fast path of the malloc shim itself. */
typedef struct MemallocSlabRegistry {
    pthread_rwlock_t rwlock;
    uintptr_t* table;  /* open-addressed hash table, 0 = empty slot */
    size_t capacity;
    size_t count;
} MemallocSlabRegistry;

void memalloc_slabregistry_init(MemallocSlabRegistry* reg);
void memalloc_slabregistry_destroy(MemallocSlabRegistry* reg);

void memalloc_slabregistry_insert(MemallocSlabRegistry* reg, uintptr_t slab_base);
bool memalloc_slabregistry_contains(MemallocSlabRegistry* reg, uintptr_t slab_base);

#endif  /* MEMALLOC_SLAB_REGISTRY_H */
