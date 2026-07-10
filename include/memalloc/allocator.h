#ifndef MEMALLOC_ALLOCATOR_H
#define MEMALLOC_ALLOCATOR_H

#include <stddef.h>

/* Allocates at least `size` bytes, 16-byte aligned. Requests of
 * MEMALLOC_SLAB_THRESHOLD bytes or fewer are served by a slab pool; larger
 * requests go to the boundary-tag free list. Returns NULL on failure. */
void* memalloc_allocate(size_t size);

/* Frees a pointer previously returned by memalloc_allocate/reallocate.
 * `p == NULL` is a no-op. Aborts on double-free of a free-list allocation. */
void memalloc_deallocate(void* p);

/* Resizes an allocation, copying its contents. `p == NULL` behaves like
 * memalloc_allocate(new_size); `new_size == 0` behaves like
 * memalloc_deallocate(p) and returns NULL. */
void* memalloc_reallocate(void* p, size_t new_size);

/* Returns the usable size of an allocation, which may be larger than the
 * size originally requested. */
size_t memalloc_usable_size(void* p);

#endif  /* MEMALLOC_ALLOCATOR_H */
