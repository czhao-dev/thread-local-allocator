#ifndef MEMALLOC_COMMON_H
#define MEMALLOC_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* Requests at or below this size are served by the slab pools; larger
 * requests fall through to the boundary-tag free list. */
#define MEMALLOC_SLAB_THRESHOLD ((size_t)512)

/* Size classes served by the slab allocator. */
static const size_t memalloc_slab_size_classes[] = {8, 16, 32, 64, 128, 256, 512};
#define MEMALLOC_NUM_SLAB_SIZE_CLASSES \
    (sizeof(memalloc_slab_size_classes) / sizeof(memalloc_slab_size_classes[0]))

/* Alignment guarantee provided to callers (matches max_align_t on x86-64/ARM64). */
#define MEMALLOC_DEFAULT_ALIGN ((size_t)16)

static inline size_t memalloc_align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

/* Returns the smallest slab size class that can hold `size`, or 0 if `size`
 * exceeds the largest size class (and should go to the free list instead). */
static inline size_t memalloc_slab_class_for(size_t size) {
    for (size_t i = 0; i < MEMALLOC_NUM_SLAB_SIZE_CLASSES; ++i) {
        if (size <= memalloc_slab_size_classes[i]) return memalloc_slab_size_classes[i];
    }
    return 0;
}

#endif  /* MEMALLOC_COMMON_H */
