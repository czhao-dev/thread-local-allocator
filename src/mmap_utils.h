#ifndef MEMALLOC_MMAP_UTILS_H
#define MEMALLOC_MMAP_UTILS_H

#include <stddef.h>

/* Maps an anonymous, zero-filled region of `size` bytes (rounded up to a
 * page boundary by the kernel). Returns NULL on failure. */
void* memalloc_mmap_region(size_t size);

/* Maps an anonymous region of `size` bytes whose base address is aligned to
 * `alignment` (which must be a power of two and a multiple of the page
 * size). Returns NULL on failure. The returned region can later be
 * released with `munmap(ptr, size)` directly -- the kernel allows partial
 * unmaps of the over-allocation that this function trims away. */
void* memalloc_mmap_aligned(size_t size, size_t alignment);

/* Unmaps a region previously returned by memalloc_mmap_region/memalloc_mmap_aligned. */
void memalloc_munmap_region(void* ptr, size_t size);

#endif  /* MEMALLOC_MMAP_UTILS_H */
