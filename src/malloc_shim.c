/* Drop-in malloc/free/calloc/realloc replacement, intended to be loaded via
 * LD_PRELOAD (Linux) or DYLD_INSERT_LIBRARIES (macOS). See README "Use as a
 * drop-in replacement". */

#include <stddef.h>
#include <string.h>

#include "memalloc/allocator.h"

__attribute__((visibility("default"))) void* malloc(size_t size) {
    return memalloc_allocate(size == 0 ? 1 : size);
}

__attribute__((visibility("default"))) void free(void* ptr) { memalloc_deallocate(ptr); }

__attribute__((visibility("default"))) void* calloc(size_t count, size_t size) {
    size_t total;
    if (__builtin_mul_overflow(count, size, &total)) return NULL;
    void* p = memalloc_allocate(total == 0 ? 1 : total);
    if (p) memset(p, 0, total);
    return p;
}

__attribute__((visibility("default"))) void* realloc(void* ptr, size_t size) {
    if (!ptr) return memalloc_allocate(size == 0 ? 1 : size);
    if (size == 0) {
        memalloc_deallocate(ptr);
        return NULL;
    }
    return memalloc_reallocate(ptr, size);
}

__attribute__((visibility("default"))) size_t malloc_usable_size(void* ptr) {
    return memalloc_usable_size(ptr);
}
